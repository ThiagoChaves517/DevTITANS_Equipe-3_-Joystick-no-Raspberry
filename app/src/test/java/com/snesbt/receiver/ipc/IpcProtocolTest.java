package com.snesbt.receiver.ipc;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import com.snesbt.receiver.core.ControllerState;
import com.snesbt.receiver.protocol.Crc8;

import org.junit.Test;

public class IpcProtocolTest {

    private static byte[] bytes(int... v) {
        byte[] b = new byte[v.length];
        for (int i = 0; i < v.length; i++) b[i] = (byte) v[i];
        return b;
    }

    /** O mesmo vetor está em daemon/tests/daemon_tests.cpp: trava o contrato entre os dois lados. */
    @Test public void stateMessageLayout() {
        byte[] m = IpcProtocol.encode(IpcProtocol.TYPE_STATE, 0x1234, new ControllerState(0xFEFE, ControllerState.GRIP_OPEN));
        assertArrayEquals(bytes(0x53, 0x44, 0x01, 0x02, 0x34, 0x12, 0xFE, 0xFE, 0x01, 0x00, 0x00, 0x81), m);
        assertEquals(m[11] & 0xFF, Crc8.compute(m, 0, 11));
    }

    @Test public void parsesHelloAck() {
        byte[] ack = IpcProtocol.encode(IpcProtocol.TYPE_HELLO_ACK, 0, ControllerState.NEUTRAL);
        assertEquals(IpcProtocol.STATUS_OK, IpcProtocol.parseHelloAck(ack));
        ack[9] = 2;
        ack[11] = (byte) Crc8.compute(ack, 0, 11);
        assertEquals(2, IpcProtocol.parseHelloAck(ack));
    }

    @Test public void rejectsInvalidHelloAck() {
        byte[] ack = IpcProtocol.encode(IpcProtocol.TYPE_HELLO_ACK, 0, ControllerState.NEUTRAL);
        ack[11] ^= 1;
        assertEquals(-1, IpcProtocol.parseHelloAck(ack));
        assertEquals(-1, IpcProtocol.parseHelloAck(IpcProtocol.encode(IpcProtocol.TYPE_STATE, 0, ControllerState.NEUTRAL)));
        assertEquals(-1, IpcProtocol.parseHelloAck(new byte[5]));
        assertEquals(-1, IpcProtocol.parseHelloAck(null));
    }
}
