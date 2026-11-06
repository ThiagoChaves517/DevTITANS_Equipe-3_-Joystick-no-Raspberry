package com.snesbt.receiver.ipc;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.SystemClock;
import android.util.Log;

import com.snesbt.receiver.core.ControllerState;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayDeque;

/**
 * Cliente IPC do daemon nativo. Toda E/S acontece numa thread própria; {@link #submit} nunca
 * bloqueia e pode ser chamado de qualquer thread. Os estados vão para uma fila limitada (FIFO), para
 * que um toque curto entre duas notificações em rajada não se perca; só se a fila saturar o mais
 * antigo é descartado (cada mensagem é um estado completo, então o último sempre prevalece).
 */
public final class DaemonClient {
    public interface Listener {
        /** Chamado na thread do cliente quando o link com o daemon sobe ou cai. */
        void onDaemonLinkChanged(boolean up, String detail);
    }

    private static final String TAG = "SnesDaemonClient";
    /** Reenvio do estado enquanto houver botão pressionado, para alimentar o watchdog do daemon. */
    static final long KEEPALIVE_MS = 100;
    private static final long HANDSHAKE_TIMEOUT_MS = 1000;
    private static final long BACKOFF_MIN_MS = 250, BACKOFF_MAX_MS = 2000;
    static final int MAX_QUEUE = 64;

    private static final class Pending {
        final ControllerState state;
        final long rxNanos;

        Pending(ControllerState state, long rxNanos) {
            this.state = state;
            this.rxNanos = rxNanos;
        }
    }

    private final String socketName;
    private final Listener listener;
    private final Object lock = new Object();

    // Protegidos por lock.
    private ControllerState latest = ControllerState.NEUTRAL;
    private final ArrayDeque<Pending> queue = new ArrayDeque<>();
    private boolean running = false;
    private LocalSocket socket;
    private int connection = 0;

    // Somente na thread de escrita.
    private volatile OutputStream output; // anulado por closeSocket() sob lock
    private int seq = 0;
    private long lastWriteMs = 0;
    private long backoffMs = BACKOFF_MIN_MS;
    private boolean announcedFailure = false;

    // Estatísticas (escritas na thread de escrita, lidas para diagnóstico).
    private volatile long sent = 0, writeFailures = 0, connects = 0, dropped = 0;
    private volatile long lastLatencyUs = 0, maxLatencyUs = 0, latencySumUs = 0, latencySamples = 0;
    private volatile boolean up = false;
    private volatile String lastError = "";

    private Thread writer;

    public DaemonClient(String socketName, Listener listener) {
        this.socketName = socketName;
        this.listener = listener;
    }

    public void start() {
        synchronized (lock) {
            if (running) return;
            running = true;
        }
        writer = new Thread(this::writerLoop, "snes-ipc");
        writer.start();
    }

    /** Tenta enviar estado neutro e encerra a conexão. */
    public void stop() {
        synchronized (lock) {
            if (!running) return;
            latest = ControllerState.NEUTRAL;
            queue.clear();
            queue.add(new Pending(ControllerState.NEUTRAL, 0));
            running = false;
            lock.notifyAll();
        }
        try {
            writer.join(1000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        closeSocket(false, "parado");
    }

    /**
     * Publica o estado completo mais recente.
     * @param rxNanos {@link System#nanoTime()} da notificação BLE que o originou, para medir latência; 0 se não houver.
     */
    public void submit(ControllerState state, long rxNanos) {
        synchronized (lock) {
            latest = state;
            queue.add(new Pending(state, rxNanos));
            if (queue.size() > MAX_QUEUE) {
                queue.poll();
                dropped++;
            }
            lock.notifyAll();
        }
    }

    public boolean isUp() { return up; }

    public String describe() {
        long avg = latencySamples == 0 ? 0 : latencySumUs / latencySamples;
        return "ipc up=" + up + " socket=@" + socketName + " sent=" + sent + " dropped=" + dropped + " writeFailures=" + writeFailures
                + " connects=" + connects + " latencyUs(last/avg/max)=" + lastLatencyUs + "/" + avg + "/" + maxLatencyUs
                + (lastError.isEmpty() ? "" : " lastError=" + lastError);
    }

    private void writerLoop() {
        while (true) {
            boolean stopping;
            synchronized (lock) { stopping = !running; }
            if (output == null) {
                if (stopping || !connect()) {
                    if (stopping) return;
                    sleepBackoff();
                    continue;
                }
            }
            Pending next;
            OutputStream out;
            synchronized (lock) {
                while (running && queue.isEmpty()) {
                    long wait = 0; // 0 = indefinido
                    if (!latest.isNeutral()) {
                        wait = KEEPALIVE_MS - (SystemClock.uptimeMillis() - lastWriteMs);
                        if (wait <= 0) break;
                    }
                    try { lock.wait(wait); } catch (InterruptedException e) { return; }
                    if (output == null) break; // leitor detectou queda
                }
                out = output;
                if (out == null) continue;
                next = queue.isEmpty() ? new Pending(latest, 0) : queue.poll(); // vazio = keepalive
                stopping = !running && queue.isEmpty();
            }
            write(out, next.state, next.rxNanos); // em falha, connect() ressincroniza com o estado completo

            if (stopping) return;
        }
    }

    private boolean connect() {
        LocalSocket s = new LocalSocket();
        try {
            s.connect(new LocalSocketAddress(socketName, LocalSocketAddress.Namespace.ABSTRACT));
            s.setSoTimeout((int) HANDSHAKE_TIMEOUT_MS);
            OutputStream out = s.getOutputStream();
            InputStream in = s.getInputStream();
            out.write(IpcProtocol.encode(IpcProtocol.TYPE_HELLO, seq++, ControllerState.NEUTRAL));
            out.flush();
            byte[] ack = new byte[IpcProtocol.SIZE];
            readFully(in, ack);
            int status = IpcProtocol.parseHelloAck(ack);
            if (status != IpcProtocol.STATUS_OK) throw new IOException("HELLO_ACK inválido/status=" + status);
            s.setSoTimeout(0);
            int id;
            synchronized (lock) {
                socket = s;
                id = ++connection;
                // Reconexão: descarta o histórico e envia só o estado completo atual.
                queue.clear();
                queue.add(new Pending(latest, 0));
            }
            synchronized (lock) { output = out; }
            backoffMs = BACKOFF_MIN_MS;
            announcedFailure = false;
            connects++;
            up = true;
            lastError = "";
            Log.i(TAG, "Conectado ao daemon @" + socketName);
            listener.onDaemonLinkChanged(true, "conectado");
            startReader(in, id);
            return true;
        } catch (IOException e) {
            try { s.close(); } catch (IOException ignored) {}
            lastError = e.getMessage();
            if (!announcedFailure) {
                // Loga só a primeira falha de cada sequência, sem stack trace, para não inundar o logcat.
                Log.w(TAG, "Daemon indisponível @" + socketName + ": " + e.getMessage() + " (tentando novamente)");
                announcedFailure = true;
                listener.onDaemonLinkChanged(false, "indisponível: " + e.getMessage());
            }
            return false;
        }
    }

    /** O daemon não envia nada após o HELLO_ACK; ler serve apenas para detectar EOF imediatamente. */
    private void startReader(InputStream in, int id) {
        Thread reader = new Thread(() -> {
            byte[] buf = new byte[IpcProtocol.SIZE];
            String why = "EOF";
            try {
                while (in.read(buf) >= 0) { /* descarta */ }
            } catch (IOException e) {
                why = e.getMessage();
            }
            synchronized (lock) {
                if (id != connection || socket == null) return;
            }
            closeSocket(true, "conexão perdida: " + why);
        }, "snes-ipc-reader");
        reader.setDaemon(true);
        reader.start();
    }

    private boolean write(OutputStream out, ControllerState state, long rxNanos) {
        int type = state.isNeutral() ? IpcProtocol.TYPE_NEUTRAL : IpcProtocol.TYPE_STATE;
        try {
            out.write(IpcProtocol.encode(type, seq++, state));
            out.flush();
        } catch (IOException e) {
            writeFailures++;
            closeSocket(true, "falha de escrita: " + e.getMessage());
            return false;
        }
        lastWriteMs = SystemClock.uptimeMillis();
        sent++;
        if (rxNanos != 0) {
            long us = (System.nanoTime() - rxNanos) / 1000;
            lastLatencyUs = us;
            if (us > maxLatencyUs) maxLatencyUs = us;
            latencySumUs += us;
            latencySamples++;
        }
        return true;
    }

    private void closeSocket(boolean notify, String why) {
        LocalSocket s;
        synchronized (lock) {
            s = socket;
            socket = null;
            output = null;
            lock.notifyAll();
        }
        if (s == null) return;
        try { s.close(); } catch (IOException ignored) {}
        up = false;
        lastError = why;
        Log.w(TAG, "Link com o daemon caiu: " + why);
        if (notify) listener.onDaemonLinkChanged(false, why);
    }

    private void sleepBackoff() {
        // submit() também notifica o lock; só sai antes do prazo se o cliente for parado.
        long deadline = SystemClock.uptimeMillis() + backoffMs;
        synchronized (lock) {
            long left;
            while (running && (left = deadline - SystemClock.uptimeMillis()) > 0) {
                try { lock.wait(left); } catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
            }
        }
        backoffMs = Math.min(BACKOFF_MAX_MS, backoffMs * 2);
    }

    private static void readFully(InputStream in, byte[] buf) throws IOException {
        int n = 0;
        while (n < buf.length) {
            int r = in.read(buf, n, buf.length - n);
            if (r < 0) throw new IOException("EOF no handshake");
            n += r;
        }
    }
}
