package com.snesbt.receiver;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.util.Log;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class SnesDaemonClient {

    private static final String TAG =
            "SnesDaemonClient";

    private static final String SOCKET_PATH =
            "/data/local/tmp/snes_uinput.sock";

    private LocalSocket socket;
    private OutputStream output;

    public boolean connect() {

        if (socket != null && socket.isConnected()) {
            return true;
        }

        try {

            socket = new LocalSocket();

            socket.connect(
                    new LocalSocketAddress(
                            SOCKET_PATH,
                            LocalSocketAddress.Namespace.FILESYSTEM
                    )
            );

            output = socket.getOutputStream();

            Log.i(
                    TAG,
                    "Conectado ao daemon"
            );

            return true;

        } catch (IOException e) {

            Log.e(
                    TAG,
                    "Falha ao conectar ao daemon",
                    e
            );

            socket = null;
            output = null;

            return false;
        }
    }

    public synchronized boolean sendState(int state) {

        if (output == null) {

            Log.e(
                    TAG,
                    "Socket não conectado"
            );

            return false;
        }

        try {

            byte[] data =
                    ByteBuffer
                            .allocate(2)
                            .order(ByteOrder.LITTLE_ENDIAN)
                            .putShort((short) state)
                            .array();

            output.write(data);
            output.flush();

            Log.d(
                    TAG,
                    String.format(
                            "Estado enviado: 0x%04X",
                            state & 0xFFFF
                    )
            );

            return true;

        } catch (IOException e) {

            Log.e(
                    TAG,
                    "Erro ao enviar estado",
                    e
            );

            disconnect();

            return false;
        }
    }

    public synchronized void disconnect() {

        if (socket == null) {
            return;
        }

        try {
            socket.close();

        } catch (IOException e) {

            Log.e(
                    TAG,
                    "Erro ao fechar socket",
                    e
            );
        }

        socket = null;
        output = null;

        Log.i(
                TAG,
                "Desconectado do daemon"
        );
    }

    public boolean isConnected() {

        return socket != null &&
                socket.isConnected();
    }
}