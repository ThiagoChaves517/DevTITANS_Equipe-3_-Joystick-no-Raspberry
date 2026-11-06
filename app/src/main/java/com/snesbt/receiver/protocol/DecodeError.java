package com.snesbt.receiver.protocol;

/** Motivos de rejeição de frame, na ordem em que são verificados (seção 5.1). */
public enum DecodeError {
    INVALID_LENGTH,
    INVALID_SYNC,
    UNSUPPORTED_VERSION,
    UNKNOWN_TYPE,
    CRC_MISMATCH,
    SEQUENCE_ERROR
}
