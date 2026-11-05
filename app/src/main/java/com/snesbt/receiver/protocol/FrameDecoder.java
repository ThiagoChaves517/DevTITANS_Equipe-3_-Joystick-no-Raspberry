package com.snesbt.receiver.protocol;

import java.util.EnumMap;
import java.util.Map;

/**
 * Converte uma notificação BLE em {@link Frame}, validando na ordem da seção 5.1.
 * Cada notificação deve conter exatamente um frame (confirmado no firmware: BleService::updateFrame
 * notifica 8 bytes por chamada, abaixo do MTU padrão de 20 bytes de payload).
 * Não é thread-safe: deve ser usado a partir de uma única thread.
 */
public final class FrameDecoder {

    /** Resultado de {@link #decode}: exatamente um entre {@code frame} e {@code error} é não nulo. */
    public static final class Result {
        public final Frame frame;
        public final DecodeError error;

        private Result(Frame frame, DecodeError error) {
            this.frame = frame;
            this.error = error;
        }

        public boolean ok() { return frame != null; }
    }

    private final SequenceTracker sequence = new SequenceTracker();
    private final Map<DecodeError, Long> errors = new EnumMap<>(DecodeError.class);
    private long valid = 0;

    public Result decode(byte[] data) {
        if (data == null || data.length != Frame.SIZE) return fail(DecodeError.INVALID_LENGTH);
        if ((data[0] & 0xFF) != Frame.SYNC1 || (data[1] & 0xFF) != Frame.SYNC2) return fail(DecodeError.INVALID_SYNC);
        if ((data[2] & 0xFF) != Frame.VERSION) return fail(DecodeError.UNSUPPORTED_VERSION);
        FrameType type = FrameType.fromCode(data[3] & 0xFF);
        if (type == null) return fail(DecodeError.UNKNOWN_TYPE);
        if ((data[7] & 0xFF) != Crc8.compute(data, 0, 7)) return fail(DecodeError.CRC_MISMATCH);
        if (!sequence.accept(data[4])) return fail(DecodeError.SEQUENCE_ERROR);
        valid++;
        return new Result(new Frame(type, data[4], data[5], data[6]), null);
    }

    /** Reinicia a referência de sequência (nova conexão). Contadores são preservados. */
    public void resetSequence() { sequence.reset(); }

    public long validCount() { return valid; }
    public long errorCount(DecodeError e) { Long n = errors.get(e); return n == null ? 0 : n; }
    public long lostFrames() { return sequence.lostFrames(); }
    public long sequenceResyncs() { return sequence.resyncs(); }

    private Result fail(DecodeError e) {
        Long n = errors.get(e);
        errors.put(e, n == null ? 1 : n + 1);
        return new Result(null, e);
    }
}
