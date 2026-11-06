package com.snesbt.receiver.core;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.snesbt.receiver.core.ControllerCore.Outcome;
import com.snesbt.receiver.protocol.Frame;
import com.snesbt.receiver.protocol.FrameType;

import org.junit.Test;

public class ControllerCoreTest {

    private static Frame state(int buttons) {
        return new Frame(FrameType.CONTROLLER_STATE, 0, buttons & 0xFF, buttons >> 8);
    }

    private static Frame grip(int g) { return new Frame(FrameType.GRIP_STATE, 0, g, 0); }

    @Test public void startsNeutral() {
        ControllerCore c = new ControllerCore();
        assertTrue(c.current().isNeutral());
        assertFalse(c.neutralize());
        assertEquals(0, c.neutralizations());
    }

    @Test public void pressAndReleaseProduceChangesOnlyOnTransitions() {
        ControllerCore c = new ControllerCore();
        int bPressed = 0xFFFF & ~(1 << ControllerState.BIT_B);
        assertEquals(Outcome.CHANGED, c.apply(state(bPressed)));
        assertTrue(c.current().isPressed(ControllerState.BIT_B));
        assertEquals(Outcome.UNCHANGED, c.apply(state(bPressed)));
        assertEquals(Outcome.CHANGED, c.apply(state(0xFFFF)));
        assertFalse(c.current().isPressed(ControllerState.BIT_B));
        assertEquals(bPressed, c.previous().buttons);
    }

    @Test public void mapsEveryFirmwareBit() {
        int[] bits = {ControllerState.BIT_B, ControllerState.BIT_Y, ControllerState.BIT_SELECT, ControllerState.BIT_START,
                ControllerState.BIT_UP, ControllerState.BIT_DOWN, ControllerState.BIT_LEFT, ControllerState.BIT_RIGHT,
                ControllerState.BIT_A, ControllerState.BIT_X, ControllerState.BIT_L, ControllerState.BIT_R};
        for (int i = 0; i < bits.length; i++) assertEquals(i, bits[i]); // mesma ordem de SnesState.h
        for (int bit : bits) {
            ControllerState s = new ControllerState(0xFFFF & ~(1 << bit), 0);
            for (int other : bits) assertEquals(bit == other, s.isPressed(other));
        }
    }

    @Test public void unusedHighBitsAreIgnored() {
        ControllerCore c = new ControllerCore();
        assertEquals(Outcome.UNCHANGED, c.apply(state(0x0FFF))); // bits 12-15 em 0 não são botões
        assertTrue(c.current().isNeutral());
    }

    @Test public void gripStateTracked() {
        ControllerCore c = new ControllerCore();
        assertEquals(Outcome.UNCHANGED, c.apply(grip(ControllerState.GRIP_NEUTRAL)));
        assertEquals(Outcome.CHANGED, c.apply(grip(ControllerState.GRIP_CLOSE)));
        assertEquals(ControllerState.GRIP_CLOSE, c.current().grip);
        assertEquals(Outcome.INVALID_PAYLOAD, c.apply(grip(3)));
        assertEquals(ControllerState.GRIP_CLOSE, c.current().grip);
        assertEquals(1, c.invalidPayloads());
    }

    @Test public void nonStateTypesIgnored() {
        ControllerCore c = new ControllerCore();
        assertEquals(Outcome.IGNORED, c.apply(new Frame(FrameType.PING, 0, 0, 0)));
        assertEquals(Outcome.IGNORED, c.apply(new Frame(FrameType.ACCEL_STATE, 0, 1, 1)));
        assertEquals(Outcome.IGNORED, c.apply(new Frame(FrameType.TRANSPORT_SWITCH, 0, 1, 0)));
        assertTrue(c.current().isNeutral());
        assertEquals(3, c.ignoredFrames());
    }

    @Test public void neutralizeReleasesOnceAndClearsGrip() {
        ControllerCore c = new ControllerCore();
        c.apply(state(0xFEFE));
        c.apply(grip(ControllerState.GRIP_OPEN));
        assertTrue(c.neutralize());
        assertTrue(c.current().isNeutral());
        assertFalse(c.neutralize()); // sem liberação repetida
        assertEquals(1, c.neutralizations());
    }

    @Test public void deterministicForSameSequence() {
        int[] seq = {0xFFFF, 0xFFFE, 0xFEFE, 0xFEFF, 0xFFFF, 0xFFEF};
        StringBuilder a = new StringBuilder(), b = new StringBuilder();
        for (StringBuilder out : new StringBuilder[]{a, b}) {
            ControllerCore c = new ControllerCore();
            for (int s : seq) out.append(c.apply(state(s))).append(c.current()).append(';');
        }
        assertEquals(a.toString(), b.toString());
    }

    @Test public void describesPressedButtons() {
        assertEquals("0xFEFE [B A] grip=0", new ControllerState(0xFEFE, 0).toString());
    }
}
