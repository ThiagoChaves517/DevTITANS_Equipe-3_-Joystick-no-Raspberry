package com.snesbt.receiver.protocol;

/**
 * Política de sequência (docs/protocol_spec.md §4).
 *
 * O firmware usa um único contador de 8 bits para todos os tipos de frame, então a
 * verificação é global. Delta 0 = duplicado; delta &gt; 128 = atrasado/fora de ordem; ambos
 * são rejeitados. Delta &gt; 1 é aceito, contando os frames perdidos. Após
 * {@link #RESYNC_AFTER} rejeições consecutivas a referência é reiniciada, para não
 * bloquear indefinidamente caso o emissor reinicie o contador sem desconectar.
 */
public final class SequenceTracker {
    public static final int RESYNC_AFTER = 4;

    private int last = -1;
    private int consecutiveErrors = 0;
    private long lostFrames = 0;
    private long resyncs = 0;

    /** @return true se a sequência for aceita. */
    public boolean accept(int sequence) {
        sequence &= 0xFF;
        if (last < 0) {
            last = sequence;
            return true;
        }
        int delta = (sequence - last) & 0xFF;
        if (delta == 0 || delta > 128) {
            if (++consecutiveErrors >= RESYNC_AFTER) {
                resyncs++;
                consecutiveErrors = 0;
                last = sequence;
                return true;
            }
            return false;
        }
        lostFrames += delta - 1;
        consecutiveErrors = 0;
        last = sequence;
        return true;
    }

    /** Chamado a cada nova conexão BLE: o primeiro frame define a referência. */
    public void reset() {
        last = -1;
        consecutiveErrors = 0;
    }

    public long lostFrames() { return lostFrames; }
    public long resyncs() { return resyncs; }
}
