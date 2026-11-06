package com.snesbt.receiver.core;

import java.util.Locale;

/**
 * Estado completo e normalizado do controle. Imutável.
 *
 * {@code buttons} segue o registrador SNES do firmware (SnesState.h): 16 bits, ativo em LOW
 * (bit 0 = pressionado). Os bits 12–15 não são usados pelo SNES e são sempre forçados a 1.
 * {@code grip} vem do GRIP_STATE: 0 = neutro, 1 = abrir, 2 = fechar.
 */
public final class ControllerState {
    public static final int BIT_B = 0, BIT_Y = 1, BIT_SELECT = 2, BIT_START = 3,
            BIT_UP = 4, BIT_DOWN = 5, BIT_LEFT = 6, BIT_RIGHT = 7,
            BIT_A = 8, BIT_X = 9, BIT_L = 10, BIT_R = 11;
    private static final String[] NAMES = {"B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R"};

    public static final int ALL_RELEASED = 0xFFFF;
    static final int UNUSED_BITS = 0xF000;

    public static final int GRIP_NEUTRAL = 0, GRIP_OPEN = 1, GRIP_CLOSE = 2;

    public static final ControllerState NEUTRAL = new ControllerState(ALL_RELEASED, GRIP_NEUTRAL);

    public final int buttons;
    public final int grip;

    public ControllerState(int buttons, int grip) {
        this.buttons = (buttons | UNUSED_BITS) & 0xFFFF;
        this.grip = grip;
    }

    public boolean isPressed(int bit) { return (buttons & (1 << bit)) == 0; }

    public boolean isNeutral() { return buttons == ALL_RELEASED && grip == GRIP_NEUTRAL; }

    ControllerState withButtons(int b) { return new ControllerState(b, grip); }

    ControllerState withGrip(int g) { return new ControllerState(buttons, g); }

    @Override public boolean equals(Object o) {
        if (!(o instanceof ControllerState)) return false;
        ControllerState s = (ControllerState) o;
        return s.buttons == buttons && s.grip == grip;
    }

    @Override public int hashCode() { return buttons * 31 + grip; }

    /** Ex.: {@code 0xFEFE [B A] grip=0}. */
    @Override public String toString() {
        StringBuilder sb = new StringBuilder(String.format(Locale.ROOT, "0x%04X [", buttons));
        boolean any = false;
        for (int bit = 0; bit < NAMES.length; bit++) {
            if (isPressed(bit)) { sb.append(any ? " " : "").append(NAMES[bit]); any = true; }
        }
        return sb.append("] grip=").append(grip).toString();
    }
}
