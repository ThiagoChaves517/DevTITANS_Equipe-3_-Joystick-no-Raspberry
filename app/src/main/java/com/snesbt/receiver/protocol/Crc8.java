package com.snesbt.receiver.protocol;

/** CRC-8/ATM: polinômio 0x07, init 0x00, sem reflexão, sem XOR final (igual ao firmware). */
public final class Crc8 {
    private Crc8() {}

    public static int compute(byte[] data, int offset, int length) {
        int crc = 0;
        for (int i = offset; i < offset + length; i++) {
            crc ^= data[i] & 0xFF;
            for (int bit = 0; bit < 8; bit++) {
                crc = (crc & 0x80) != 0 ? ((crc << 1) ^ 0x07) & 0xFF : (crc << 1) & 0xFF;
            }
        }
        return crc;
    }
}
