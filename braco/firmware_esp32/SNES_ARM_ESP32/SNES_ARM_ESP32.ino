/*
 * Firmware do ESP32 do braço robótico (Equipe Robô).
 *
 * Recebe da Raspberry Pi (snes_arm_bridge) quadros de texto pela USB serial
 * (115200 8N1) e move os 4 servos com rampa, limites e watchdog.
 * Protocolo completo em braco/PROTOCOLO.md:
 *
 *   J <base> <ombro> <cotovelo> <garra>   ângulos-alvo (graus), a 50 Hz
 *   G <0-100>                             abertura da garra em %
 *   H                                     volta para home
 *   S                                     congela onde está
 *   P                                     ping -> OK
 *   I                                     limites -> I <min max home> x4
 *   # texto                               comentário (ignorado)
 *
 * Respostas: READY <versão> no boot, OK, ERR <motivo>, T <4 ângulos> a 10 Hz
 * e logs começando com "#".
 *
 * Dá para testar sem a Raspberry: Monitor Serial a 115200, final de linha
 * "Nova linha", e digitar os comandos (ex.: "J 90 90 120 60").
 */

#include <Arduino.h>

#include "ArmConfig.h"
#include "ServoJoint.h"

namespace
{
    ServoJoint joints[ArmConfig::JOINT_COUNT];

    char line[64];
    size_t lineLen = 0;
    bool lineOverflow = false;

    uint32_t lastFrameMs = 0;
    uint32_t lastUpdateMs = 0;
    uint32_t lastTelemetryMs = 0;

    bool streaming = false; /* recebendo J: watchdog ativo */
    bool homing = false;
}

static void holdAll()
{
    for (auto &joint : joints)
        joint.hold();
}

static void startHoming()
{
    homing = true;
    streaming = false;

    for (auto &joint : joints)
        joint.setTarget(joint.config().homeDeg);
}

static void printInfo()
{
    Serial.print("I");

    for (const auto &joint : joints)
    {
        const ArmConfig::Joint &cfg = joint.config();
        Serial.printf(" %.0f %.0f %.0f", cfg.minDeg, cfg.maxDeg, cfg.homeDeg);
    }

    Serial.println();
}

static void printHelp()
{
    Serial.println("# comandos: J b o c g | G 0-100 | H | S | P | I");
    Serial.println("# juntas: base ombro cotovelo garra (graus)");
}

static void handleLine(char *text)
{
    while (*text == ' ')
        ++text;

    if (*text == '\0' || *text == '#')
        return;

    const char cmd = text[0];
    const uint32_t now = millis();

    switch (cmd)
    {
    case 'J':
    {
        float deg[4];

        if (sscanf(text + 1, "%f %f %f %f", &deg[0], &deg[1], &deg[2], &deg[3]) != 4)
        {
            Serial.println("ERR formato: J base ombro cotovelo garra");
            return;
        }

        for (uint8_t j = 0; j < ArmConfig::JOINT_COUNT; ++j)
            joints[j].setTarget(deg[j]);

        lastFrameMs = now;
        streaming = true;
        homing = false;
        return; /* sem OK: J chega a 50 Hz e serve de heartbeat */
    }

    case 'G':
    {
        float pct;

        if (sscanf(text + 1, "%f", &pct) != 1 || pct < 0.0f || pct > 100.0f)
        {
            Serial.println("ERR formato: G 0-100");
            return;
        }

        ServoJoint &garra = joints[3];
        const ArmConfig::Joint &cfg = garra.config();
        garra.setTarget(cfg.minDeg + pct / 100.0f * (cfg.maxDeg - cfg.minDeg));
        lastFrameMs = now;
        Serial.println("OK");
        return;
    }

    case 'H':
        startHoming();
        Serial.println("OK");
        return;

    case 'S':
        holdAll();
        streaming = false;
        homing = false;
        Serial.println("OK");
        return;

    case 'P':
        Serial.println("OK");
        return;

    case 'I':
        printInfo();
        return;

    case '?':
        printHelp();
        return;

    default:
        Serial.printf("ERR comando desconhecido: %c\n", cmd);
        return;
    }
}

static void readSerial()
{
    while (Serial.available() > 0)
    {
        const char c = static_cast<char>(Serial.read());

        if (c == '\n' || c == '\r')
        {
            if (lineOverflow)
                Serial.println("ERR linha longa demais");
            else if (lineLen > 0)
            {
                line[lineLen] = '\0';
                handleLine(line);
            }

            lineLen = 0;
            lineOverflow = false;
        }
        else if (lineLen + 1 < sizeof(line))
        {
            line[lineLen++] = c;
        }
        else
        {
            lineOverflow = true;
        }
    }
}

void setup()
{
    Serial.begin(ArmConfig::SERIAL_BAUD);

    for (uint8_t j = 0; j < ArmConfig::JOINT_COUNT; ++j)
        joints[j].begin(ArmConfig::JOINTS[j], j);

    delay(50);

    Serial.println();
    Serial.printf("READY %s\n", ArmConfig::FIRMWARE_VERSION);

    for (const auto &joint : joints)
    {
        const ArmConfig::Joint &cfg = joint.config();
        Serial.printf("# %-8s GPIO%-2u %3.0f..%3.0f home %3.0f\n",
                      cfg.name, cfg.pin, cfg.minDeg, cfg.maxDeg, cfg.homeDeg);
    }

    printHelp();

    lastUpdateMs = lastTelemetryMs = millis();
}

void loop()
{
    readSerial();

    const uint32_t now = millis();

    /*
     * Segurança: se a Raspberry parar de mandar J (cabo solto, bridge
     * travada), o braço congela em vez de continuar até o último alvo.
     */
    if (streaming && now - lastFrameMs > ArmConfig::WATCHDOG_MS)
    {
        streaming = false;
        holdAll();
        Serial.printf("# WATCHDOG: %lu ms sem quadro J, servos parados\n",
                      static_cast<unsigned long>(ArmConfig::WATCHDOG_MS));
    }

    if (now - lastUpdateMs >= ArmConfig::UPDATE_MS)
    {
        const float dt = (now - lastUpdateMs) / 1000.0f;
        const float speed = homing ? ArmConfig::HOME_SPEED_DEG_S : ArmConfig::MAX_SPEED_DEG_S;

        lastUpdateMs = now;

        bool allHome = true;

        for (auto &joint : joints)
        {
            joint.update(dt, speed);
            allHome &= joint.current() == joint.target();
        }

        if (homing && allHome)
            homing = false;
    }

    if (now - lastTelemetryMs >= ArmConfig::TELEMETRY_MS)
    {
        lastTelemetryMs = now;
        Serial.printf("T %.0f %.0f %.0f %.0f\n",
                      joints[0].current(), joints[1].current(),
                      joints[2].current(), joints[3].current());
    }
}
