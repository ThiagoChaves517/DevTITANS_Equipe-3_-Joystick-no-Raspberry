package com.snesbt.receiver.core;

import com.snesbt.receiver.protocol.Frame;

/**
 * Estado lógico do controle. Independente de Android, BLE e IPC; deve ser usado de uma única thread.
 *
 * O mapeamento acelerômetro → botões (cima→X, baixo→B, direita→A, esquerda→Y) é feito no
 * firmware (AccelerometerInput.cpp), que já entrega o resultado como bits de CONTROLLER_STATE.
 * Por isso ACCEL_STATE, que o firmware atual não emite, é tratado como tipo ignorado.
 */
public final class ControllerCore {

    /** Efeito de um frame sobre o estado. */
    public enum Outcome {
        /** Estado mudou; publicar {@link #current()}. */
        CHANGED,
        /** Frame de estado válido sem mudança (renova o watchdog). */
        UNCHANGED,
        /** Tipo válido que não afeta o estado do controle (não renova o watchdog). */
        IGNORED,
        /** Payload fora do domínio do tipo; estado preservado. */
        INVALID_PAYLOAD
    }

    private ControllerState current = ControllerState.NEUTRAL;
    private ControllerState previous = ControllerState.NEUTRAL;
    private long ignored = 0, invalidPayload = 0, neutralizations = 0;

    public Outcome apply(Frame frame) {
        switch (frame.type) {
            case CONTROLLER_STATE:
                return update(current.withButtons(frame.payloadU16()));
            case GRIP_STATE:
                if (frame.payload0 > ControllerState.GRIP_CLOSE) {
                    invalidPayload++;
                    return Outcome.INVALID_PAYLOAD;
                }
                return update(current.withGrip(frame.payload0));
            default:
                ignored++;
                return Outcome.IGNORED;
        }
    }

    /**
     * Libera todos os botões (perda de BLE, watchdog, encerramento).
     * @return true se o estado mudou; false se já estava neutro (não gera liberação repetida).
     */
    public boolean neutralize() {
        if (current.isNeutral()) return false;
        neutralizations++;
        update(ControllerState.NEUTRAL);
        return true;
    }

    public ControllerState current() { return current; }
    public ControllerState previous() { return previous; }
    public long ignoredFrames() { return ignored; }
    public long invalidPayloads() { return invalidPayload; }
    public long neutralizations() { return neutralizations; }

    private Outcome update(ControllerState next) {
        if (next.equals(current)) return Outcome.UNCHANGED;
        previous = current;
        current = next;
        return Outcome.CHANGED;
    }
}
