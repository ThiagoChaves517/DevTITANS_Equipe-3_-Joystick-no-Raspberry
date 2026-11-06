package com.snesbt.receiver.core;

/**
 * Detecta ausência de frames de estado válidos. Lógica pura com relógio injetado;
 * o agendamento da verificação fica a cargo de quem usa (ControllerService).
 */
public final class Watchdog {
    private final long timeoutMs;
    private long lastFeedMs;
    private boolean armed = false;
    private long expirations = 0;

    public Watchdog(long timeoutMs) {
        if (timeoutMs <= 0) throw new IllegalArgumentException("timeoutMs deve ser > 0");
        this.timeoutMs = timeoutMs;
    }

    /** Frame de estado válido recebido. */
    public void feed(long nowMs) {
        lastFeedMs = nowMs;
        armed = true;
    }

    /** Desarma sem disparar (ex.: desconexão já tratada). */
    public void disarm() { armed = false; }

    /**
     * @return true exatamente uma vez por período sem frames; depois fica desarmado até o próximo feed.
     */
    public boolean poll(long nowMs) {
        if (!armed || nowMs - lastFeedMs < timeoutMs) return false;
        armed = false;
        expirations++;
        return true;
    }

    /** Milissegundos até expirar, ou -1 se desarmado. */
    public long remainingMs(long nowMs) {
        return armed ? Math.max(0, timeoutMs - (nowMs - lastFeedMs)) : -1;
    }

    public boolean isArmed() { return armed; }
    public long timeoutMs() { return timeoutMs; }
    public long expirations() { return expirations; }
}
