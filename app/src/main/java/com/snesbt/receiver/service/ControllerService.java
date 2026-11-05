package com.snesbt.receiver.service;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;

import com.snesbt.receiver.ble.BleTransport;
import com.snesbt.receiver.ble.BleTransport.LinkState;
import com.snesbt.receiver.core.ControllerCore;
import com.snesbt.receiver.core.ControllerState;
import com.snesbt.receiver.core.Watchdog;
import com.snesbt.receiver.ipc.DaemonClient;
import com.snesbt.receiver.ipc.IpcProtocol;
import com.snesbt.receiver.protocol.DecodeError;
import com.snesbt.receiver.protocol.FrameDecoder;
import com.snesbt.receiver.protocol.FrameType;

import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.EnumMap;
import java.util.Map;

/**
 * Serviço headless (foreground, tipo connectedDevice) que coordena BLE → protocolo → core → IPC.
 *
 * Toda a lógica roda numa única HandlerThread ("snes-core"); o cliente IPC tem thread própria e
 * nunca é chamado de forma bloqueante. Iniciar/parar via ADB:
 * <pre>
 * adb shell am start-foreground-service -n com.snesbt.receiver/.service.ControllerService [--el watchdog_ms 150]
 * adb shell am start-foreground-service -n com.snesbt.receiver/.service.ControllerService -a com.snesbt.receiver.STOP
 * adb shell dumpsys activity service com.snesbt.receiver/.service.ControllerService
 * </pre>
 */
public final class ControllerService extends Service implements BleTransport.Listener, DaemonClient.Listener {
    public static final String ACTION_STOP = "com.snesbt.receiver.STOP";
    public static final String EXTRA_WATCHDOG_MS = "watchdog_ms";
    /**
     * Somente em build debug: injeta notificações como se viessem do BLE (teste no dispositivo sem o ESP32).
     * Extras: {@code frames} = notificações em hex separadas por vírgula; {@code interval_ms} entre elas.
     */
    public static final String ACTION_INJECT = "com.snesbt.receiver.INJECT";
    /** Ver docs/DECISOES.md (ADR-007) para a origem deste valor. */
    public static final long DEFAULT_WATCHDOG_MS = 100;

    private static final String TAG = "SnesBtReceiver";
    private static final String CHANNEL = "snesbt";
    private static final int NOTIFICATION_ID = 1;
    private static final long INVALID_LOG_INTERVAL_MS = 1000;

    /** Resumo de uma linha para a Activity de diagnóstico. */
    private static volatile String summary = ServiceState.STOPPED.name();

    public static String summary() { return summary; }

    private HandlerThread thread;
    private Handler handler;
    private BleTransport ble;
    private DaemonClient daemon;

    // Somente na thread "snes-core".
    private final FrameDecoder decoder = new FrameDecoder();
    private final ControllerCore core = new ControllerCore();
    private Watchdog watchdog;
    private boolean watchdogPosted = false;
    private ServiceState state = ServiceState.STOPPED;
    private LinkState bleState = LinkState.IDLE;
    private String bleDetail = "";
    private boolean daemonUp = false;
    private boolean wasReady = false;
    private String errorDetail = null;
    private long lastValidMs = 0;
    private long firstValidAfterConnectMs = 0;
    private final Map<DecodeError, Long> lastInvalidLogMs = new EnumMap<>(DecodeError.class);

    @Override public void onCreate() {
        super.onCreate();
        thread = new HandlerThread("snes-core");
        thread.start();
        handler = new Handler(thread.getLooper());
        ble = new BleTransport(this, handler, this);
        daemon = new DaemonClient(IpcProtocol.SOCKET_NAME, this);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (!enterForeground()) {
            stopSelf();
            return START_NOT_STICKY;
        }
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            Log.i(TAG, "STOP solicitado");
            stopSelf();
            return START_NOT_STICKY;
        }
        if (intent != null && ACTION_INJECT.equals(intent.getAction())) {
            inject(intent.getStringExtra("frames"), intent.getIntExtra("interval_ms", 10));
            return START_STICKY;
        }
        long timeout = intent == null ? DEFAULT_WATCHDOG_MS : intent.getLongExtra(EXTRA_WATCHDOG_MS, DEFAULT_WATCHDOG_MS);
        handler.post(() -> startComponents(timeout));
        return START_STICKY;
    }

    private void inject(String frames, int intervalMs) {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            Log.w(TAG, "INJECT recusado: disponível apenas em build debug");
            return;
        }
        if (frames == null || frames.isEmpty()) return;
        String[] list = frames.split(",");
        Log.i(TAG, "INJECT: " + list.length + " notificações, intervalo " + intervalMs + " ms");
        handler.post(() -> { if (state == ServiceState.STOPPED) startComponents(DEFAULT_WATCHDOG_MS); });
        for (int i = 0; i < list.length; i++) {
            final byte[] value = hexToBytes(list[i].trim());
            // Mesmo caminho de BleTransport.deliver(): decoder, core, watchdog e IPC.
            handler.postDelayed(() -> onNotification(value, System.nanoTime()), (long) i * intervalMs);
        }
    }

    private static byte[] hexToBytes(String hex) {
        byte[] out = new byte[hex.length() / 2];
        for (int i = 0; i < out.length; i++) out[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        return out;
    }

    private boolean enterForeground() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "SNES-BT", NotificationManager.IMPORTANCE_LOW));
        try {
            startForeground(NOTIFICATION_ID, buildNotification("Iniciando"), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            return true;
        } catch (RuntimeException e) {
            // Sem BLUETOOTH_CONNECT o Android 14 recusa o tipo connectedDevice.
            Log.e(TAG, "ERROR: não foi possível entrar em foreground: " + e.getMessage()
                    + ". Conceda: adb shell pm grant " + getPackageName() + " android.permission.BLUETOOTH_CONNECT");
            summary = ServiceState.ERROR + ": " + e.getMessage();
            return false;
        }
    }

    // ---- ciclo de vida (thread snes-core) ----

    private void startComponents(long watchdogMs) {
        if (state != ServiceState.STOPPED) {
            Log.i(TAG, "Serviço já em execução (" + state + ")");
            return;
        }
        transition(ServiceState.STARTING, "watchdog=" + watchdogMs + " ms");
        if (watchdogMs <= 0) { fatal("watchdog_ms inválido: " + watchdogMs); return; }
        watchdog = new Watchdog(watchdogMs);
        if (!hasPermission(Manifest.permission.BLUETOOTH_SCAN) || !hasPermission(Manifest.permission.BLUETOOTH_CONNECT)) {
            fatal("permissões BLUETOOTH_SCAN/BLUETOOTH_CONNECT ausentes; conceda com adb shell pm grant " + getPackageName()
                    + " android.permission.BLUETOOTH_SCAN (e BLUETOOTH_CONNECT) e reinicie o serviço");
            return;
        }
        daemon.start();
        ble.start();
        refreshState("componentes iniciados");
    }

    @Override public void onDestroy() {
        Log.i(TAG, "Encerrando serviço");
        handler.post(() -> {
            ble.stop();
            neutralize("encerramento");
        });
        thread.quitSafely();
        try { thread.join(500); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        daemon.stop(); // envia NEUTRAL antes de fechar, se conectado
        summary = ServiceState.STOPPED.name();
        Log.i(TAG, "Estado: " + state + " -> STOPPED");
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }

    // ---- BLE (thread snes-core) ----

    @Override public void onLinkState(LinkState link, String detail) {
        LinkState prev = bleState;
        bleState = link;
        bleDetail = detail;
        if (link == LinkState.READY && prev != LinkState.READY) {
            decoder.resetSequence();
            firstValidAfterConnectMs = 0;
        }
        if (prev == LinkState.READY && link != LinkState.READY) {
            watchdog.disarm();
            neutralize("perda do link BLE: " + detail);
        }
        refreshState(detail);
    }

    @Override public void onNotification(byte[] value, long rxNanos) {
        FrameDecoder.Result r = decoder.decode(value);
        if (!r.ok()) {
            logInvalid(r.error, value);
            return;
        }
        ControllerCore.Outcome outcome = core.apply(r.frame);
        switch (outcome) {
            case CHANGED:
                Log.i(TAG, "estado " + core.current() + " (seq " + r.frame.sequence + ")");
                daemon.submit(core.current(), rxNanos);
                // fallthrough: frame de estado válido renova o watchdog
            case UNCHANGED:
                feedWatchdog();
                break;
            case INVALID_PAYLOAD:
                Log.w(TAG, "payload inválido: " + r.frame);
                break;
            case IGNORED:
                if (r.frame.type == FrameType.ACCEL_CALIBRATED) {
                    Log.i(TAG, "acelerometro calibrado (variacao " + r.frame.payload1 + " ADC, seq " + r.frame.sequence + ")");
                }
                break;
        }
    }

    private void feedWatchdog() {
        long now = SystemClock.uptimeMillis();
        lastValidMs = now;
        if (firstValidAfterConnectMs == 0) {
            firstValidAfterConnectMs = now;
            Log.i(TAG, "primeiro frame válido após conexão");
        }
        watchdog.feed(now);
        if (!watchdogPosted) {
            watchdogPosted = true;
            handler.postDelayed(watchdogCheck, watchdog.timeoutMs());
        }
    }

    /** Reagenda a si mesma só enquanto armada: sem frames, não há tarefa periódica. */
    private final Runnable watchdogCheck = new Runnable() {
        @Override public void run() {
            watchdogPosted = false;
            long now = SystemClock.uptimeMillis();
            if (watchdog.poll(now)) {
                neutralize("watchdog: " + (now - lastValidMs) + " ms sem frame de estado válido");
            } else if (watchdog.isArmed()) {
                watchdogPosted = true;
                handler.postDelayed(this, Math.max(1, watchdog.remainingMs(now)));
            }
        }
    };

    private void neutralize(String why) {
        if (core.neutralize()) {
            Log.w(TAG, "NEUTRALIZADO (" + why + ")");
            daemon.submit(ControllerState.NEUTRAL, 0);
        }
    }

    private void logInvalid(DecodeError error, byte[] value) {
        long now = SystemClock.uptimeMillis();
        Long last = lastInvalidLogMs.get(error);
        if (last != null && now - last < INVALID_LOG_INTERVAL_MS) return;
        lastInvalidLogMs.put(error, now);
        StringBuilder hex = new StringBuilder();
        if (value != null) for (byte b : value) hex.append(String.format("%02X ", b));
        Log.w(TAG, "frame rejeitado " + error + " len=" + (value == null ? 0 : value.length) + " [" + hex.toString().trim()
                + "] total=" + decoder.errorCount(error));
    }

    // ---- daemon (thread snes-ipc → snes-core) ----

    @Override public void onDaemonLinkChanged(boolean up, String detail) {
        handler.post(() -> {
            daemonUp = up;
            refreshState("daemon " + detail);
        });
    }

    // ---- estado do serviço ----

    private void fatal(String detail) {
        errorDetail = detail;
        refreshState(detail);
    }

    private void refreshState(String detail) {
        ServiceState next = ServiceState.derive(errorDetail != null, daemonUp, bleState, wasReady);
        if (next == ServiceState.READY) wasReady = true;
        if (next != state) transition(next, detail);
        summary = state + " | " + bleDetail + " | daemon " + (daemonUp ? "ok" : "indisponível") + " | " + core.current();
    }

    private void transition(ServiceState next, String detail) {
        if (next == ServiceState.ERROR) Log.e(TAG, "Estado: " + state + " -> ERROR: " + detail);
        else Log.i(TAG, "Estado: " + state + " -> " + next + " (" + detail + ")");
        state = next;
        getSystemService(NotificationManager.class).notify(NOTIFICATION_ID, buildNotification(next.name()));
    }

    private Notification buildNotification(String text) {
        return new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
                .setContentTitle("SNES-BT Receiver")
                .setContentText(text)
                .setOngoing(true)
                .build();
    }

    private boolean hasPermission(String p) {
        return checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED;
    }

    // ---- diagnóstico: adb shell dumpsys activity service com.snesbt.receiver/.service.ControllerService ----

    @Override protected void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        Object done = new Object();
        final String[] out = new String[1];
        // Lê o estado na própria thread do core para obter um retrato consistente.
        boolean posted = handler.post(() -> {
            out[0] = dumpOnCore();
            synchronized (done) { done.notifyAll(); }
        });
        synchronized (done) {
            if (posted && out[0] == null) {
                try { done.wait(500); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
            }
        }
        pw.println(out[0] != null ? out[0] : "snes-core não respondeu; resumo: " + summary);
    }

    private String dumpOnCore() {
        long now = SystemClock.uptimeMillis();
        StringBuilder sb = new StringBuilder();
        sb.append("state=").append(state).append(errorDetail != null ? " error=" + errorDetail : "").append('\n');
        sb.append(ble.describe()).append(" detail=").append(bleDetail).append('\n');
        sb.append(daemon.describe()).append('\n');
        sb.append("controller=").append(core.current())
                .append(" lastValidAgeMs=").append(lastValidMs == 0 ? -1 : now - lastValidMs)
                .append(" neutralizations=").append(core.neutralizations())
                .append(" watchdogMs=").append(watchdog == null ? -1 : watchdog.timeoutMs())
                .append(" watchdogExpirations=").append(watchdog == null ? 0 : watchdog.expirations()).append('\n');
        sb.append("frames valid=").append(decoder.validCount())
                .append(" lost(seq gaps)=").append(decoder.lostFrames())
                .append(" seqResyncs=").append(decoder.sequenceResyncs())
                .append(" ignoredTypes=").append(core.ignoredFrames())
                .append(" invalidPayload=").append(core.invalidPayloads());
        for (DecodeError e : DecodeError.values()) sb.append(' ').append(e).append('=').append(decoder.errorCount(e));
        return sb.toString();
    }

    /** Inicia o serviço a partir de um componente em primeiro plano (Activity, boot). */
    public static void start(Context context) {
        context.startForegroundService(new Intent(context, ControllerService.class));
    }
}
