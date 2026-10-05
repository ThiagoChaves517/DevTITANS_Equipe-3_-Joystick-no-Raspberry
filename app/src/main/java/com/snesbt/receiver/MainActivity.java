package com.snesbt.receiver;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import java.util.UUID;

public class MainActivity extends AppCompatActivity {

    private static final String TAG =
            "SnesBtReceiver";

    private static final String DEVICE_NAME =
            "SNES-BT-Controller";

    private static final UUID SERVICE_UUID =
            UUID.fromString(
                    "8b7e0001-5a9d-4c31-9f20-7d6a8e42b101"
            );

    private static final UUID STATE_CHARACTERISTIC_UUID =
            UUID.fromString(
                    "8b7e0002-5a9d-4c31-9f20-7d6a8e42b101"
            );

    private static final UUID CLIENT_CHARACTERISTIC_CONFIG_UUID =
            UUID.fromString(
                    "00002902-0000-1000-8000-00805f9b34fb"
            );

    private static final int REQUEST_BLUETOOTH_PERMISSIONS =
            1001;

    private BluetoothAdapter bluetoothAdapter;

    private BluetoothLeScanner bluetoothLeScanner;

    private BluetoothGatt bluetoothGatt;

    private SnesDaemonClient daemonClient;


    // =========================================================
    // Activity
    // =========================================================

    @Override
    protected void onCreate(Bundle savedInstanceState) {

        super.onCreate(savedInstanceState);

        setContentView(
                R.layout.activity_main
        );

        Log.i(
                TAG,
                "================================"
        );

        Log.i(
                TAG,
                " SNES BT Receiver"
        );

        Log.i(
                TAG,
                "================================"
        );


        // -----------------------------------------------------
        // Daemon
        // -----------------------------------------------------

        daemonClient =
                new SnesDaemonClient();

        if (!daemonClient.connect()) {

            Log.e(
                    TAG,
                    "Não foi possível conectar ao daemon"
            );
        }


        // -----------------------------------------------------
        // Bluetooth
        // -----------------------------------------------------

        BluetoothManager bluetoothManager =
                (BluetoothManager)
                        getSystemService(
                                Context.BLUETOOTH_SERVICE
                        );

        if (bluetoothManager == null) {

            Log.e(
                    TAG,
                    "BluetoothManager não disponível"
            );

            return;
        }

        bluetoothAdapter =
                bluetoothManager.getAdapter();

        if (bluetoothAdapter == null) {

            Log.e(
                    TAG,
                    "BluetoothAdapter não disponível"
            );

            return;
        }


        // -----------------------------------------------------
        // Permissões
        // -----------------------------------------------------

        if (!hasBluetoothPermissions()) {

            requestBluetoothPermissions();

            return;
        }


        // -----------------------------------------------------
        // BLE scan
        // -----------------------------------------------------

        startBleScan();
    }


    // =========================================================
    // Bluetooth permissions
    // =========================================================

    private boolean hasBluetoothPermissions() {

        if (android.os.Build.VERSION.SDK_INT < 31) {
            return true;
        }

        return ActivityCompat.checkSelfPermission(
                this,
                Manifest.permission.BLUETOOTH_SCAN
        ) == PackageManager.PERMISSION_GRANTED
                &&
                ActivityCompat.checkSelfPermission(
                        this,
                        Manifest.permission.BLUETOOTH_CONNECT
                ) == PackageManager.PERMISSION_GRANTED;
    }


    private void requestBluetoothPermissions() {

        if (android.os.Build.VERSION.SDK_INT < 31) {
            return;
        }

        ActivityCompat.requestPermissions(
                this,
                new String[]{
                        Manifest.permission.BLUETOOTH_SCAN,
                        Manifest.permission.BLUETOOTH_CONNECT
                },
                REQUEST_BLUETOOTH_PERMISSIONS
        );
    }


    @Override
    public void onRequestPermissionsResult(
            int requestCode,
            @NonNull String[] permissions,
            @NonNull int[] grantResults) {

        super.onRequestPermissionsResult(
                requestCode,
                permissions,
                grantResults
        );

        if (requestCode !=
                REQUEST_BLUETOOTH_PERMISSIONS) {

            return;
        }

        if (hasBluetoothPermissions()) {

            Log.i(
                    TAG,
                    "Permissões Bluetooth concedidas"
            );

            startBleScan();

        } else {

            Log.e(
                    TAG,
                    "Permissões Bluetooth não concedidas"
            );
        }
    }


    // =========================================================
    // BLE scan
    // =========================================================

    private void startBleScan() {

        if (!hasBluetoothPermissions()) {

            Log.e(
                    TAG,
                    "Sem permissões Bluetooth"
            );

            return;
        }

        if (bluetoothAdapter == null) {

            Log.e(
                    TAG,
                    "BluetoothAdapter nulo"
            );

            return;
        }

        if (!bluetoothAdapter.isEnabled()) {

            Log.e(
                    TAG,
                    "Bluetooth está desligado"
            );

            return;
        }

        bluetoothLeScanner =
                bluetoothAdapter.getBluetoothLeScanner();

        if (bluetoothLeScanner == null) {

            Log.e(
                    TAG,
                    "BluetoothLeScanner não disponível"
            );

            return;
        }

        Log.i(
                TAG,
                "Iniciando scan BLE..."
        );

        bluetoothLeScanner.startScan(
                scanCallback
        );
    }


    private final ScanCallback scanCallback =
            new ScanCallback() {

                @Override
                public void onScanResult(
                        int callbackType,
                        ScanResult result) {

                    BluetoothDevice device =
                            result.getDevice();

                    if (!hasBluetoothPermissions()) {
                        return;
                    }

                    String name =
                            device.getName();

                    Log.d(
                            TAG,
                            "BLE device: " + name
                    );

                    if (DEVICE_NAME.equals(name)) {

                        Log.i(
                                TAG,
                                "SNES-BT-Controller encontrado"
                        );

                        if (bluetoothLeScanner != null) {

                            bluetoothLeScanner.stopScan(
                                    this
                            );
                        }

                        connectToDevice(device);
                    }
                }


                @Override
                public void onScanFailed(
                        int errorCode) {

                    Log.e(
                            TAG,
                            "BLE scan falhou: "
                                    + errorCode
                    );
                }
            };


    // =========================================================
    // BLE connection
    // =========================================================

    private void connectToDevice(
            BluetoothDevice device) {

        if (!hasBluetoothPermissions()) {
            return;
        }

        Log.i(
                TAG,
                "Conectando ao SNES-BT-Controller..."
        );

        bluetoothGatt =
                device.connectGatt(
                        this,
                        false,
                        gattCallback
                );
    }


    private final BluetoothGattCallback gattCallback =
            new BluetoothGattCallback() {

                @Override
                public void onConnectionStateChange(
                        BluetoothGatt gatt,
                        int status,
                        int newState) {

                    super.onConnectionStateChange(
                            gatt,
                            status,
                            newState
                    );

                    if (newState ==
                            BluetoothGatt.STATE_CONNECTED) {

                        Log.i(
                                TAG,
                                "BLE conectado"
                        );

                        if (hasBluetoothPermissions()) {

                            gatt.discoverServices();
                        }

                    } else if (newState ==
                            BluetoothGatt.STATE_DISCONNECTED) {

                        Log.i(
                                TAG,
                                "BLE desconectado"
                        );
                    }
                }


                @Override
                public void onServicesDiscovered(
                        BluetoothGatt gatt,
                        int status) {

                    super.onServicesDiscovered(
                            gatt,
                            status
                    );

                    if (status !=
                            BluetoothGatt.GATT_SUCCESS) {

                        Log.e(
                                TAG,
                                "Falha ao descobrir serviços: "
                                        + status
                        );

                        return;
                    }

                    Log.i(
                            TAG,
                            "Serviços BLE descobertos"
                    );

                    BluetoothGattCharacteristic
                            characteristic =
                            getStateCharacteristic(gatt);

                    if (characteristic == null) {

                        Log.e(
                                TAG,
                                "Characteristic de estado não encontrada"
                        );

                        return;
                    }

                    enableNotifications(
                            gatt,
                            characteristic
                    );
                }


                @Override
                public void onDescriptorWrite(
                        BluetoothGatt gatt,
                        BluetoothGattDescriptor descriptor,
                        int status) {

                    super.onDescriptorWrite(
                            gatt,
                            descriptor,
                            status
                    );

                    if (status ==
                            BluetoothGatt.GATT_SUCCESS) {

                        Log.i(
                                TAG,
                                "Notificações BLE habilitadas"
                        );

                    } else {

                        Log.e(
                                TAG,
                                "Falha ao habilitar notificações: "
                                        + status
                        );
                    }
                }


                @Override
                public void onCharacteristicChanged(
                        BluetoothGatt gatt,
                        BluetoothGattCharacteristic characteristic,
                        byte[] value) {

                    super.onCharacteristicChanged(
                            gatt,
                            characteristic,
                            value
                    );

                    if (!STATE_CHARACTERISTIC_UUID.equals(
                            characteristic.getUuid())) {

                        return;
                    }

                    handleState(value);
                }


                @Override
                public void onCharacteristicChanged(
                        BluetoothGatt gatt,
                        BluetoothGattCharacteristic characteristic) {

                    super.onCharacteristicChanged(
                            gatt,
                            characteristic
                    );

                    if (!STATE_CHARACTERISTIC_UUID.equals(
                            characteristic.getUuid())) {

                        return;
                    }

                    byte[] value =
                            characteristic.getValue();

                    handleState(value);
                }
            };


    // =========================================================
    // BLE service / characteristic
    // =========================================================

    private BluetoothGattCharacteristic
    getStateCharacteristic(
            BluetoothGatt gatt) {

        android.bluetooth.BluetoothGattService service =
                gatt.getService(
                        SERVICE_UUID
                );

        if (service == null) {

            Log.e(
                    TAG,
                    "Service SNES não encontrada"
            );

            return null;
        }

        return service.getCharacteristic(
                STATE_CHARACTERISTIC_UUID
        );
    }


    private void enableNotifications(
            BluetoothGatt gatt,
            BluetoothGattCharacteristic characteristic) {

        if (!hasBluetoothPermissions()) {
            return;
        }

        boolean result =
                gatt.setCharacteristicNotification(
                        characteristic,
                        true
                );

        Log.i(
                TAG,
                "setCharacteristicNotification: "
                        + result
        );

        BluetoothGattDescriptor descriptor =
                characteristic.getDescriptor(
                        CLIENT_CHARACTERISTIC_CONFIG_UUID
                );

        if (descriptor == null) {

            Log.e(
                    TAG,
                    "CCCD não encontrado"
            );

            return;
        }

        descriptor.setValue(
                BluetoothGattDescriptor
                        .ENABLE_NOTIFICATION_VALUE
        );

        gatt.writeDescriptor(
                descriptor
        );
    }


    // =========================================================
    // SNES state
    // =========================================================

    private void handleState(
            byte[] value) {

        if (value == null ||
                value.length < 2) {

            Log.e(
                    TAG,
                    "Estado BLE inválido"
            );

            return;
        }

        /*
         * ESP32 envia:
         *
         * byte 0 = low byte
         * byte 1 = high byte
         *
         * Exemplo:
         *
         * 0xFEFF
         *
         * recebe:
         *
         * FF FE
         */

        int state =
                (value[0] & 0xFF)
                        |
                ((value[1] & 0xFF) << 8);


        // -----------------------------------------------------
        // Log do estado recebido pelo BLE
        // -----------------------------------------------------

        Log.i(
                TAG,
                String.format(
                        "[INPUT] State = 0x%04X",
                        state
                )
        );


        // -----------------------------------------------------
        // Envia para o daemon
        // -----------------------------------------------------

        if (daemonClient == null) {

            Log.e(
                    TAG,
                    "Daemon client nulo"
            );

            return;
        }

        if (!daemonClient.isConnected()) {

            Log.w(
                    TAG,
                    "Daemon desconectado. Tentando reconectar..."
            );

            if (!daemonClient.connect()) {

                Log.e(
                        TAG,
                        "Falha ao reconectar ao daemon"
                );

                return;
            }
        }

        daemonClient.sendState(
                state
        );
    }


    // =========================================================
    // Activity destroy
    // =========================================================

    @Override
    protected void onDestroy() {

        super.onDestroy();


        // -----------------------------------------------------
        // BLE scan
        // -----------------------------------------------------

        if (bluetoothLeScanner != null &&
                hasBluetoothPermissions()) {

            bluetoothLeScanner.stopScan(
                    scanCallback
            );
        }


        // -----------------------------------------------------
        // BLE connection
        // -----------------------------------------------------

        if (bluetoothGatt != null) {

            bluetoothGatt.close();

            bluetoothGatt = null;
        }


        // -----------------------------------------------------
        // Daemon
        // -----------------------------------------------------

        if (daemonClient != null) {

            daemonClient.disconnect();

            daemonClient = null;
        }
    }
}