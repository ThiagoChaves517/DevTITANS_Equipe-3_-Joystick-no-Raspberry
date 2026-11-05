package com.snesbt.receiver.protocol;

import java.util.Locale;

/** Frame v1 validado e tipado. Imutável. */
public final class Frame {
    public static final int SIZE = 8;
    public static final int SYNC1 = 0x53;
    public static final int SYNC2 = 0x4E;
    public static final int VERSION = 0x01;

    public final FrameType type;
    public final int sequence;
    public final int payload0;
    public final int payload1;

    public Frame(FrameType type, int sequence, int payload0, int payload1) {
        this.type = type;
        this.sequence = sequence & 0xFF;
        this.payload0 = payload0 & 0xFF;
        this.payload1 = payload1 & 0xFF;
    }

    /** Payload como inteiro de 16 bits little-endian (usado por CONTROLLER_STATE). */
    public int payloadU16() { return payload0 | (payload1 << 8); }

    /** Codifica um frame com CRC; usado em testes e ferramentas de diagnóstico. */
    public static byte[] encode(int typeCode, int sequence, int payload0, int payload1) {
        byte[] out = new byte[SIZE];
        out[0] = (byte) SYNC1;
        out[1] = (byte) SYNC2;
        out[2] = (byte) VERSION;
        out[3] = (byte) typeCode;
        out[4] = (byte) sequence;
        out[5] = (byte) payload0;
        out[6] = (byte) payload1;
        out[7] = (byte) Crc8.compute(out, 0, 7);
        return out;
    }

    @Override public String toString() {
        return String.format(Locale.ROOT, "%s seq=%d p=%02X %02X", type, sequence, payload0, payload1);
    }
}
