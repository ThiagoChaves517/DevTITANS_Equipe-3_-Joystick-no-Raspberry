package com.snesbt.receiver.ble;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.ParcelUuid;
import android.util.Log;

import java.util.Arrays;
import java.util.List;
import java.util.UUID;

/**
 * Transporte BLE/GATT. Não interpreta o conteúdo dos frames: entrega cada notificação da
 * característica de estado ao {@link Listener}. Todas as transições e callbacks rodam no
 * {@link Handler} recebido (serializado com o resto do serviço); callbacks do Binder apenas
 * repassam para ele. Callbacks de conexões antigas são descartados comparando o objeto GATT.
 *
 * As permissões BLUETOOTH_SCAN/CONNECT são verificadas pelo serviço antes de {@link #start()};
 * uma SecurityException aqui vira erro reportado e nova tentativa.
 */
@SuppressLint("MissingPermission")
public final class BleTransport {
    public enum LinkState { IDLE, SCANNING, CONNECTING, DISCOVERING, READY }

    public interface Listener {
        void onLinkState(LinkState state, String detail);
        void onNotification(byte[] value, long rxNanos);
    }

    public static final UUID SERVICE_UUID = UUID.fromString("8b7e0001-5a9d-4c31-9f20-7d6a8e42b101");
    public static final UUID STATE_UUID = UUID.fromString("8b7e0002-5a9d-4c31-9f20-7d6a8e42b101");
    public static final String DEVICE_NAME = "SNES-BT-Controller";
    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    private static final String TAG = "SnesBle";
    /** Ative com: adb shell setprop log.tag.SnesBleFrames VERBOSE */
    private static final String FRAME_TAG = "SnesBleFrames";
    private static final long SCAN_TIMEOUT_MS = 20_000;
    private static final long SETUP_TIMEOUT_MS = 10_000;
    private static final long BACKOFF_MIN_MS = 500, BACKOFF_MAX_MS = 8_000;

    private final Context context;
    private final Handler handler;
    private final Listener listener;

    private boolean running = false;
    private LinkState state = LinkState.IDLE;
    private BluetoothLeScanner scanner;
    private boolean scanning = false;
    private BluetoothGatt gatt;
    private String deviceAddress = "";
    private long backoffMs = BACKOFF_MIN_MS;

    private volatile long notifications = 0;
    private long connects = 0, disconnects = 0, scanFailures = 0;

    private final Runnable retry = this::beginScan;
    private final Runnable scanTimeout = () -> {
        if (state == LinkState.SCANNING) {
            stopScan();
            fail("nenhum " + DEVICE_NAME + " encontrado em " + SCAN_TIMEOUT_MS / 1000 + " s");
        }
    };
    private final Runnable setupTimeout = () -> {
        if (state == LinkState.CONNECTING || state == LinkState.DISCOVERING) {
            closeGatt();
            fail("timeout de conexão/descoberta (" + SETUP_TIMEOUT_MS / 1000 + " s)");
        }
    };

    public BleTransport(Context context, Handler handler, Listener listener) {
        this.context = context.getApplicationContext();
        this.handler = handler;
        this.listener = listener;
    }

    /** Deve ser chamado na thread do handler. */
    public void start() {
        if (running) return;
        running = true;
        backoffMs = BACKOFF_MIN_MS;
        beginScan();
    }

    /** Deve ser chamado na thread do handler. */
    public void stop() {
        running = false;
        handler.removeCallbacks(retry);
        handler.removeCallbacks(scanTimeout);
        handler.removeCallbacks(setupTimeout);
        stopScan();
        closeGatt();
        setState(LinkState.IDLE, "parado");
    }

    public String describe() {
        return "ble state=" + state + " device=" + (deviceAddress.isEmpty() ? "-" : deviceAddress)
                + " notifications=" + notifications + " connects=" + connects + " disconnects=" + disconnects
                + " scanFailures=" + scanFailures + " nextBackoffMs=" + backoffMs;
    }

    // ---- scan ----

    private void beginScan() {
        if (!running) return;
        BluetoothManager manager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
        if (adapter == null || !adapter.isEnabled()) { fail("Bluetooth desligado ou indisponível"); return; }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) { fail("scanner BLE indisponível"); return; }
        // Filtros são combinados com OU: o firmware anuncia o UUID do serviço e o nome.
        List<ScanFilter> filters = Arrays.asList(
                new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE_UUID)).build(),
                new ScanFilter.Builder().setDeviceName(DEVICE_NAME).build());
        ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        try {
            scanner.startScan(filters, settings, scanCallback);
        } catch (SecurityException e) {
            fail("sem permissão BLUETOOTH_SCAN");
            return;
        }
        scanning = true;
        setState(LinkState.SCANNING, "procurando " + DEVICE_NAME);
        handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS);
    }

    private void stopScan() {
        handler.removeCallbacks(scanTimeout);
        if (!scanning || scanner == null) return;
        scanning = false;
        try { scanner.stopScan(scanCallback); } catch (SecurityException | IllegalStateException e) {
            Log.w(TAG, "stopScan: " + e);
        }
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            int rssi = result.getRssi();
            handler.post(() -> onDeviceFound(device, rssi));
        }

        @Override public void onScanFailed(int errorCode) {
            handler.post(() -> {
                scanFailures++;
                scanning = false;
                fail("falha no scan BLE: código " + errorCode);
            });
        }
    };

    private void onDeviceFound(BluetoothDevice device, int rssi) {
        if (!running || state != LinkState.SCANNING) return;
        stopScan();
        deviceAddress = device.getAddress();
        setState(LinkState.CONNECTING, "conectando a " + deviceAddress + " rssi=" + rssi);
        handler.postDelayed(setupTimeout, SETUP_TIMEOUT_MS);
        try {
            gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
        } catch (SecurityException e) {
            gatt = null;
            fail("sem permissão BLUETOOTH_CONNECT");
            return;
        }
        if (gatt == null) fail("connectGatt retornou null");
    }

    // ---- GATT ----

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            handler.post(() -> onConnectionChanged(g, status, newState));
        }

        @Override public void onServicesDiscovered(BluetoothGatt g, int status) {
            handler.post(() -> onDiscovered(g, status));
        }

        @Override public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int status) {
            handler.post(() -> onCccdWritten(g, d, status));
        }

        @Override public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
            deliver(g, c, value);
        }

        @SuppressWarnings("deprecation")
        @Override public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            // Usado apenas antes do API 33; no 33+ o framework chama a sobrecarga com value.
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) deliver(g, c, c.getValue());
        }
    };

    private void deliver(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
        if (!STATE_UUID.equals(c.getUuid())) return;
        final long rx = System.nanoTime();
        final byte[] copy = value == null ? new byte[0] : value.clone();
        notifications++;
        if (Log.isLoggable(FRAME_TAG, Log.VERBOSE)) Log.v(FRAME_TAG, "len=" + copy.length + " " + hex(copy));
        handler.post(() -> {
            if (g == gatt && state == LinkState.READY) listener.onNotification(copy, rx);
        });
    }

    private void onConnectionChanged(BluetoothGatt g, int status, int newState) {
        if (g != gatt) { safeClose(g); return; } // callback de uma conexão antiga
        if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
            connects++;
            setState(LinkState.DISCOVERING, "conectado; descobrindo serviços");
            try {
                // Intervalo de conexão curto (7,5–15 ms): o firmware notifica a cada ~10 ms.
                g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
                if (!g.discoverServices()) { closeGatt(); fail("discoverServices recusado"); }
            } catch (SecurityException e) {
                closeGatt();
                fail("sem permissão BLUETOOTH_CONNECT");
            }
        } else {
            boolean wasReady = state == LinkState.READY;
            if (wasReady) disconnects++;
            closeGatt();
            fail((wasReady ? "desconectado" : "falha na conexão") + " (status=" + status + ")");
        }
    }

    private void onDiscovered(BluetoothGatt g, int status) {
        if (g != gatt) return;
        if (status != BluetoothGatt.GATT_SUCCESS) { closeGatt(); fail("descoberta falhou: status " + status); return; }
        BluetoothGattService service = g.getService(SERVICE_UUID);
        BluetoothGattCharacteristic ch = service == null ? null : service.getCharacteristic(STATE_UUID);
        if (ch == null) { closeGatt(); fail("serviço/característica SNES-BT não encontrados"); return; }
        BluetoothGattDescriptor cccd = ch.getDescriptor(CCCD_UUID);
        if (cccd == null) { closeGatt(); fail("descritor CCCD ausente"); return; }
        try {
            if (!g.setCharacteristicNotification(ch, true)) { closeGatt(); fail("setCharacteristicNotification falhou"); return; }
            boolean ok;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                ok = g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothGatt.GATT_SUCCESS;
            } else {
                ok = writeDescriptorLegacy(g, cccd);
            }
            if (!ok) { closeGatt(); fail("escrita do CCCD recusada"); }
        } catch (SecurityException e) {
            closeGatt();
            fail("sem permissão BLUETOOTH_CONNECT");
        }
    }

    @SuppressWarnings("deprecation")
    private static boolean writeDescriptorLegacy(BluetoothGatt g, BluetoothGattDescriptor d) {
        d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
        return g.writeDescriptor(d);
    }

    private void onCccdWritten(BluetoothGatt g, BluetoothGattDescriptor d, int status) {
        if (g != gatt || !CCCD_UUID.equals(d.getUuid())) return;
        if (status != BluetoothGatt.GATT_SUCCESS) { closeGatt(); fail("CCCD retornou status " + status); return; }
        handler.removeCallbacks(setupTimeout);
        backoffMs = BACKOFF_MIN_MS;
        setState(LinkState.READY, "notificações ativas em " + deviceAddress);
    }

    // ---- utilitários ----

    private void fail(String reason) {
        handler.removeCallbacks(setupTimeout);
        if (!running) return;
        setState(LinkState.IDLE, reason + "; nova tentativa em " + backoffMs + " ms");
        handler.removeCallbacks(retry);
        handler.postDelayed(retry, backoffMs);
        backoffMs = Math.min(BACKOFF_MAX_MS, backoffMs * 2);
    }

    private void closeGatt() {
        BluetoothGatt g = gatt;
        gatt = null;
        if (g == null) return;
        try { g.disconnect(); } catch (SecurityException ignored) {}
        safeClose(g);
    }

    private static void safeClose(BluetoothGatt g) {
        try { g.close(); } catch (SecurityException ignored) {}
    }

    private void setState(LinkState next, String detail) {
        LinkState prev = state;
        state = next;
        if (prev != next || next == LinkState.IDLE) Log.i(TAG, prev + " -> " + next + ": " + detail);
        listener.onLinkState(next, detail);
    }

    private static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder(b.length * 3);
        for (byte x : b) sb.append(String.format("%02X ", x));
        return sb.toString().trim();
    }
}
