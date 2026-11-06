/*
 * Firmware do ESP32 do braço robótico (Equipe Robô).
 *
 * Segue a wiki "Braço Robótico ‐ Semana 09\09‐21\10" e o fluxograma do William:
 *   1. inicializa a Serial (115200) e os 4 servos na posição inicial;
 *   2. lê da Serial USB os quadros que a Raspberry repassa (snes_arm_daemon);
 *   3. transforma os 16 bits do controle SNES em direções -1/0/+1 (ArmMapping.h);
 *   4. a cada 25 ms cada servo anda 1 grau na sua direção, dentro dos limites;
 *   5. sem quadro por 250 ms, as direções viram 0 (segurança).
 *
 * Quadro (8 bytes, igual ao do joystick): 53 4E 01 01 seq p0 p1 CRC-8.
 * O braço NÃO responde à Raspberry: o que aparece na Serial são só logs "[ARM]"
 * para quem estiver com o Monitor Serial aberto.
 */

#include <Arduino.h>

#include "ArmConfig.h"
#include "ArmMapping.h"
#include "FrameProtocol.h"
#include "ServoJoint.h"

namespace
{
    ServoJoint joints[ArmConfig::JOINT_COUNT];
    Protocol::FrameDecoder decoder;

    uint16_t state = ArmMapping::ALL_RELEASED; /* últimos 16 bits recebidos */
    ArmMapping::Directions dirs = {0, 0, 0, 0};

    unsigned long ultimoRecebimento = 0;
    unsigned long ultimoMovimento = 0;
    unsigned long ultimoLogErro = 0;
    unsigned long errosLogados = 0;
    bool movendo = false;
}

static void printAngles(const char *why)
{
    Serial.printf("[ARM] %s: base=%d ombro=%d cotovelo=%d garra=%d\n", why,
                  joints[0].angle(), joints[1].angle(), joints[2].angle(), joints[3].angle());
}

static void applyState(uint16_t newState)
{
    newState |= 0xF000; /* bits 12-15 não são botões */

    if (newState == state)
        return;

    state = newState;
    dirs = ArmMapping::fromState(state);

    Serial.printf("[ARM] estado 0x%04X -> base=%+d ombro=%+d cotovelo=%+d garra=%+d%s\n",
                  state, dirs.base, dirs.ombro, dirs.cotovelo, dirs.garra,
                  ArmMapping::pressed(state, ArmMapping::START) ? " (START: parado)" : "");
}

static void readSerial()
{
    while (Serial.available() > 0)
    {
        Protocol::Frame frame;

        if (!decoder.push(static_cast<uint8_t>(Serial.read()), frame))
            continue;

        if (frame.type != Protocol::CONTROLLER_STATE)
            continue; /* outros tipos são válidos, mas o braço só usa o estado do controle */

        ultimoRecebimento = millis();
        applyState(frame.state());
    }

    /* Erros de quadro: no máximo um log por segundo. */
    const unsigned long erros = decoder.errors() + decoder.discarded();

    if (erros != errosLogados && millis() - ultimoLogErro >= 1000)
    {
        Serial.printf("[ARM] quadros válidos=%lu, inválidos=%lu, bytes descartados=%lu\n",
                      decoder.frames(), decoder.errors(), decoder.discarded());
        errosLogados = erros;
        ultimoLogErro = millis();
    }
}

void setup()
{
    Serial.begin(ArmConfig::SERIAL_BAUD);

    for (uint8_t j = 0; j < ArmConfig::JOINT_COUNT; ++j)
        joints[j].begin(ArmConfig::JOINTS[j], j);

    delay(50);

    Serial.println();
    Serial.printf("[ARM] %s: aguardando quadros da Raspberry\n", ArmConfig::FIRMWARE_VERSION);

    for (const auto &joint : joints)
    {
        const ArmConfig::Joint &cfg = joint.config();
        Serial.printf("[ARM] %-8s GPIO%-2u %3d..%3d inicial %3d\n",
                      cfg.name, unsigned(cfg.pin), cfg.minDeg, cfg.maxDeg, cfg.initialDeg);
    }

    ultimoMovimento = millis();
}

void loop()
{
    readSerial();

    const unsigned long agora = millis();

    /* Segurança: Raspberry parou de mandar (cabo, daemon morto) com botão pressionado. */
    if (state != ArmMapping::ALL_RELEASED && agora - ultimoRecebimento > ArmConfig::TIMEOUT_SERIAL_MS)
    {
        Serial.printf("[ARM] %lu ms sem quadro: direções zeradas\n",
                      static_cast<unsigned long>(ArmConfig::TIMEOUT_SERIAL_MS));
        applyState(ArmMapping::ALL_RELEASED);
    }

    if (agora - ultimoMovimento < ArmConfig::INTERVALO_MS)
        return;

    ultimoMovimento = agora;

    bool mudou = joints[0].step(dirs.base);
    mudou |= joints[1].step(dirs.ombro);
    mudou |= joints[2].step(dirs.cotovelo);
    mudou |= joints[3].step(dirs.garra);

    /* Log só quando o movimento termina (soltou o botão ou chegou no limite). */
    if (movendo && !mudou)
        printAngles("parado");

    movendo = mudou;
}
