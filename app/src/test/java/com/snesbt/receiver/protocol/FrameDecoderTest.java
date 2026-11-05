package com.snesbt.receiver.protocol;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.nio.charset.StandardCharsets;

import org.junit.Test;

public class FrameDecoderTest {

    private static byte[] state(int seq, int buttons) {
        return Frame.encode(FrameType.CONTROLLER_STATE.code, seq, buttons & 0xFF, buttons >> 8);
    }

    private static byte[] bytes(int... v) {
        byte[] b = new byte[v.length];
        for (int i = 0; i < v.length; i++) b[i] = (byte) v[i];
        return b;
    }

    @Test public void crc8StandardCheckValue() {
        byte[] v = "123456789".getBytes(StandardCharsets.US_ASCII);
        assertEquals(0xF4, Crc8.compute(v, 0, v.length)); // CRC-8/ATM check
    }

    /** Vetores fixos (calculados de forma independente do encoder) para travar o contrato com o firmware. */
    @Test public void fixedVectorsMatchFirmwareEncoding() {
        assertEquals(0x6F, Crc8.compute(bytes(0x53, 0x4E, 0x01, 0x01, 0x00, 0xFF, 0xFF), 0, 7));
        byte[] encoded = state(0, 0xFFFF);
        assertEquals(0x6F, encoded[7] & 0xFF);
        FrameDecoder.Result r = new FrameDecoder().decode(bytes(0x53, 0x4E, 0x01, 0x01, 0x00, 0xFF, 0xFF, 0x6F));
        assertTrue(r.ok());
    }

    /** Notificações capturadas do ESP32 real (firmware oficial, 2026-10-21), consecutivas. */
    @Test public void realFirmwareCapture() {
        FrameDecoder d = new FrameDecoder();
        FrameDecoder.Result state = d.decode(bytes(0x53, 0x4E, 0x01, 0x01, 0xFC, 0xFF, 0xFD, 0xF7));
        assertTrue(state.ok());
        assertEquals(FrameType.CONTROLLER_STATE, state.frame.type);
        assertEquals(0xFDFF, state.frame.payloadU16()); // X pressionado (GPIO4 flutuando, antes do INPUT_PULLUP)
        FrameDecoder.Result grip = d.decode(bytes(0x53, 0x4E, 0x01, 0x06, 0xFD, 0x00, 0x00, 0xD4));
        assertTrue(grip.ok());
        assertEquals(FrameType.GRIP_STATE, grip.frame.type);
        assertEquals(0, d.lostFrames()); // sequência compartilhada: FC -> FD
    }

    @Test public void decodesControllerState() {
        FrameDecoder d = new FrameDecoder();
        FrameDecoder.Result r = d.decode(state(7, 0xFEFE)); // B (bit 0) e A (bit 8) pressionados
        assertTrue(r.ok());
        assertNull(r.error);
        assertEquals(FrameType.CONTROLLER_STATE, r.frame.type);
        assertEquals(7, r.frame.sequence);
        assertEquals(0xFEFE, r.frame.payloadU16());
        assertEquals(1, d.validCount());
    }

    @Test public void decodesGripState() {
        FrameDecoder.Result r = new FrameDecoder().decode(Frame.encode(FrameType.GRIP_STATE.code, 1, 2, 0));
        assertTrue(r.ok());
        assertEquals(FrameType.GRIP_STATE, r.frame.type);
        assertEquals(2, r.frame.payload0);
    }

    @Test public void rejectsWrongLengthsIncludingNull() {
        FrameDecoder d = new FrameDecoder();
        assertEquals(DecodeError.INVALID_LENGTH, d.decode(null).error);
        assertEquals(DecodeError.INVALID_LENGTH, d.decode(new byte[0]).error);
        assertEquals(DecodeError.INVALID_LENGTH, d.decode(new byte[]{(byte) 0xFF, (byte) 0xFF}).error); // formato antigo de 2 bytes
        byte[] nine = new byte[9];
        System.arraycopy(state(0, 0xFFFF), 0, nine, 0, 8);
        assertEquals(DecodeError.INVALID_LENGTH, d.decode(nine).error);
        assertEquals(4, d.errorCount(DecodeError.INVALID_LENGTH));
    }

    @Test public void rejectsBadSync() {
        byte[] f = state(0, 0xFFFF);
        f[1] = 0x4F;
        assertEquals(DecodeError.INVALID_SYNC, new FrameDecoder().decode(f).error);
    }

    @Test public void rejectsUnsupportedVersion() {
        byte[] f = state(0, 0xFFFF);
        f[2] = 0x02;
        f[7] = (byte) Crc8.compute(f, 0, 7);
        assertEquals(DecodeError.UNSUPPORTED_VERSION, new FrameDecoder().decode(f).error);
    }

    @Test public void rejectsUnknownTypeEvenWithValidCrc() {
        byte[] f = Frame.encode(0x42, 0, 0, 0);
        assertEquals(DecodeError.UNKNOWN_TYPE, new FrameDecoder().decode(f).error);
        assertEquals(DecodeError.UNKNOWN_TYPE, new FrameDecoder().decode(Frame.encode(0x00, 0, 0, 0)).error);
    }

    @Test public void rejectsEverySingleBitFlipInPayloadOrCrc() {
        byte[] good = state(3, 0xFDFF);
        for (int i = 4; i < 8; i++) {
            for (int bit = 0; bit < 8; bit++) {
                byte[] f = good.clone();
                f[i] ^= (byte) (1 << bit);
                assertEquals("byte " + i + " bit " + bit, DecodeError.CRC_MISMATCH, new FrameDecoder().decode(f).error);
            }
        }
    }

    @Test public void invalidFrameDoesNotAdvanceSequence() {
        FrameDecoder d = new FrameDecoder();
        assertTrue(d.decode(state(10, 0xFFFF)).ok());
        byte[] bad = state(11, 0xFFFF);
        bad[7] ^= 1;
        assertFalse(d.decode(bad).ok());
        assertTrue(d.decode(state(11, 0xFFFF)).ok());
        assertEquals(0, d.lostFrames());
    }

    @Test public void sequenceSharedAcrossTypesLikeFirmware() {
        // Firmware: CONTROLLER_STATE e GRIP_STATE consomem o mesmo contador.
        FrameDecoder d = new FrameDecoder();
        assertTrue(d.decode(state(0, 0xFFFF)).ok());
        assertTrue(d.decode(Frame.encode(FrameType.GRIP_STATE.code, 1, 0, 0)).ok());
        assertTrue(d.decode(state(2, 0xFFFF)).ok());
        assertEquals(0, d.lostFrames());
    }

    @Test public void duplicateAndOldSequencesRejected() {
        FrameDecoder d = new FrameDecoder();
        assertTrue(d.decode(state(50, 0xFFFF)).ok());
        assertEquals(DecodeError.SEQUENCE_ERROR, d.decode(state(50, 0xFFFF)).error);
        assertEquals(DecodeError.SEQUENCE_ERROR, d.decode(state(49, 0xFFFF)).error);
        assertTrue(d.decode(state(51, 0xFFFF)).ok());
    }

    @Test public void sequenceGapsCountedAndWrapAroundAccepted() {
        FrameDecoder d = new FrameDecoder();
        assertTrue(d.decode(state(250, 0xFFFF)).ok());
        assertTrue(d.decode(state(254, 0xFFFF)).ok()); // 3 perdidos
        assertTrue(d.decode(state(0, 0xFFFF)).ok());   // 255 perdido, wrap
        assertTrue(d.decode(state(1, 0xFFFF)).ok());
        assertEquals(4, d.lostFrames());
    }

    @Test public void resyncsAfterPersistentSequenceErrors() {
        FrameDecoder d = new FrameDecoder();
        assertTrue(d.decode(state(200, 0xFFFF)).ok());
        // Emissor reiniciou o contador (ex.: reboot sem desconexão perceptível).
        for (int s = 0; s < SequenceTracker.RESYNC_AFTER - 1; s++) {
            assertEquals(DecodeError.SEQUENCE_ERROR, d.decode(state(100 - s, 0xFFFF)).error);
        }
        // A rejeição de número RESYNC_AFTER redefine a referência e é aceita.
        assertTrue(d.decode(state(100 - SequenceTracker.RESYNC_AFTER + 1, 0xFFFF)).ok());
        assertEquals(1, d.sequenceResyncs());
        assertTrue(d.decode(state(100 - SequenceTracker.RESYNC_AFTER + 2, 0xFFFF)).ok());
    }

    @Test public void resetSequenceAcceptsAnyFirstFrame() {
        FrameDecoder d = new FrameDecoder();
        assertTrue(d.decode(state(100, 0xFFFF)).ok());
        d.resetSequence();
        assertTrue(d.decode(state(20, 0xFFFF)).ok());
        assertEquals(0, d.lostFrames());
    }

    @Test public void reservedButKnownTypesAreValidFrames() {
        FrameDecoder d = new FrameDecoder();
        int seq = 0;
        for (FrameType t : FrameType.values()) {
            FrameDecoder.Result r = d.decode(Frame.encode(t.code, seq++, 0, 0));
            assertTrue(t.name(), r.ok());
            assertEquals(t, r.frame.type);
        }
    }
}
