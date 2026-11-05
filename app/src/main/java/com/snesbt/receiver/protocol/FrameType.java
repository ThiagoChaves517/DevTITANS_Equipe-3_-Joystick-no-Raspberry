package com.snesbt.receiver.protocol;

/** Tipos de frame conhecidos (firmware FrameProtocol.h + especificação). Ver docs/protocol_spec.md. */
public enum FrameType {
    CONTROLLER_STATE(0x01),
    HELLO(0x02),
    PING(0x03),
    CONFIG(0x04),
    ERROR(0x05),
    GRIP_STATE(0x06),
    ACCEL_STATE(0x07),
    TRANSPORT_SWITCH(0x08),
    TRANSPORT_ACK(0x09),
    ACCEL_CALIBRATED(0x0A);

    public final int code;

    FrameType(int code) { this.code = code; }

    /** @return o tipo, ou {@code null} se o código não for conhecido. */
    public static FrameType fromCode(int code) {
        for (FrameType t : values()) if (t.code == code) return t;
        return null;
    }
}
