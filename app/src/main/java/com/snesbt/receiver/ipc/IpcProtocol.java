package com.snesbt.receiver.ipc;

import com.snesbt.receiver.core.ControllerState;
import com.snesbt.receiver.protocol.Crc8;

/**
 * Protocolo IPC v1 app → daemon (docs/ipc_spec.md). Mensagens de tamanho fixo (12 bytes):
 *
 * <pre>
 *  0 'S' 0x53 | 1 'D' 0x44 | 2 versão | 3 tipo | 4-5 seq u16 LE | 6-7 botões u16 LE
 *  8 grip     | 9 status   | 10 reservado (0)   | 11 CRC-8/0x07 sobre 0..10
 * </pre>
 */
public final class IpcProtocol {
    public static final int SIZE = 12;
    public static final int MAGIC0 = 0x53, MAGIC1 = 0x44, VERSION = 0x01;

    public static final int TYPE_HELLO = 0x01;
    public static final int TYPE_STATE = 0x02;
    public static final int TYPE_NEUTRAL = 0x03;
    public static final int TYPE_HELLO_ACK = 0x81;

    public static final int STATUS_OK = 0x00;

    /** Abstract namespace (sem arquivo no disco); ver docs/ipc_spec.md §1. */
    public static final String SOCKET_NAME = "snesbt_input";

    private IpcProtocol() {}

    public static byte[] encode(int type, int seq, ControllerState state) {
        byte[] m = new byte[SIZE];
        m[0] = (byte) MAGIC0;
        m[1] = (byte) MAGIC1;
        m[2] = (byte) VERSION;
        m[3] = (byte) type;
        m[4] = (byte) seq;
        m[5] = (byte) (seq >> 8);
        m[6] = (byte) state.buttons;
        m[7] = (byte) (state.buttons >> 8);
        m[8] = (byte) state.grip;
        m[9] = 0;
        m[10] = 0;
        m[11] = (byte) Crc8.compute(m, 0, SIZE - 1);
        return m;
    }

    /** @return status do HELLO_ACK, ou -1 se a mensagem não for um HELLO_ACK v1 válido. */
    public static int parseHelloAck(byte[] m) {
        if (m == null || m.length != SIZE) return -1;
        if ((m[0] & 0xFF) != MAGIC0 || (m[1] & 0xFF) != MAGIC1 || (m[2] & 0xFF) != VERSION) return -1;
        if ((m[3] & 0xFF) != TYPE_HELLO_ACK) return -1;
        if ((m[11] & 0xFF) != Crc8.compute(m, 0, SIZE - 1)) return -1;
        return m[9] & 0xFF;
    }
}
