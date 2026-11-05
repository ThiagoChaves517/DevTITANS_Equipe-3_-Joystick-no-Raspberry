package com.snesbt.receiver.service;

import com.snesbt.receiver.ble.BleTransport.LinkState;

/** Estados do serviço (seção 7 da especificação). */
public enum ServiceState {
    STOPPED, STARTING, DAEMON_CONNECTING, BLE_SCANNING, BLE_CONNECTING, DISCOVERING_SERVICES, READY, RECOVERING, ERROR;

    /**
     * Estado derivado das condições atuais. READY exige daemon acessível (com dispositivo uinput criado,
     * confirmado pelo HELLO_ACK) e notificações BLE ativas.
     */
    public static ServiceState derive(boolean error, boolean daemonUp, LinkState ble, boolean wasReady) {
        if (error) return ERROR;
        if (daemonUp && ble == LinkState.READY) return READY;
        if (wasReady) return RECOVERING;
        if (!daemonUp) return DAEMON_CONNECTING;
        switch (ble) {
            case CONNECTING: return BLE_CONNECTING;
            case DISCOVERING: return DISCOVERING_SERVICES;
            case READY: return READY;
            default: return BLE_SCANNING;
        }
    }
}
