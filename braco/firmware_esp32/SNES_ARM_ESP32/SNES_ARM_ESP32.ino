/*
 * Firmware do ESP32 do braço robótico (Equipe Robô).
 *
 * Segue o "Diagrama de Funcionamento do Software da ESP32" do William (resources/) e a
 * wiki "Braço Robótico ‐ Semana 09\09‐21\10", com os mesmos nomes de variáveis e blocos:
 *
 *   Setup:  inicia a Serial (115200), inicializa os 4 servos e os posiciona na posição inicial.
 *   Loop:   dados seriais disponíveis? -> lê e interpreta, limitando as direções a -1/0/1
 *           tempo para atualizar os servos (>= 25 ms)? -> atualiza os ângulos (1 grau)
 *           tratamento de falhas: 250 ms sem dados -> zera as direções
 *
 * Única diferença do fluxograma: em vez de "uma linha com 4 direções", a Raspberry repassa
 * os 16 bits do controle SNES (como pede a wiki) no quadro de 8 bytes do joystick:
 *   53 4E 01 01 seq p0 p1 CRC-8
 * e as direções saem da tabela da wiki (ArmMapping.h).
 *
 * O braço NÃO responde à Raspberry: o que aparece na Serial são só logs "[ARM]".
 */

#include <Arduino.h>

#include "ArmConfig.h"
#include "ArmMapping.h"
#include "FrameProtocol.h"
#include "Servos.h"

using namespace ArmConfig;

/* ---- Variáveis principais (fluxograma) ---- */
int angBase = INICIAL_BASE;
int angOmbro = INICIAL_OMBRO;
int angCotovelo = INICIAL_COTOVELO;
int angGarra = INICIAL_GARRA;

int dirBase = 0;
int dirOmbro = 0;
int dirCotovelo = 0;
int dirGarra = 0;

unsigned long ultimoMovimento = 0;   /* última atualização dos servos */
unsigned long ultimoRecebimento = 0; /* último quadro válido (tratamento de falhas) */

/* ---- Recepção dos 16 bits ---- */
Protocol::FrameDecoder decoder;
uint16_t estado = ArmMapping::ALL_RELEASED;
unsigned long ultimoLogErro = 0;
unsigned long errosLogados = 0;
bool movendo = false;

enum Canal : uint8_t { CANAL_BASE, CANAL_OMBRO, CANAL_COTOVELO, CANAL_GARRA };

/* "Interpreta os valores e limita para -1, 0 ou 1" (tabela da wiki em ArmMapping.h). */
static void interpretaEstado(uint16_t novo)
{
    novo |= 0xF000; /* bits 12-15 não são botões */

    if (novo == estado)
        return;

    estado = novo;
    const ArmMapping::Directions d = ArmMapping::fromState(estado);
    dirBase = d.base;
    dirOmbro = d.ombro;
    dirCotovelo = d.cotovelo;
    dirGarra = d.garra;

    Serial.printf("[ARM] estado 0x%04X -> dirBase=%+d dirOmbro=%+d dirCotovelo=%+d dirGarra=%+d%s\n",
                  estado, dirBase, dirOmbro, dirCotovelo, dirGarra,
                  ArmMapping::pressed(estado, ArmMapping::START) ? " (Start: parado)" : "");
}

/* "Dados seriais disponíveis? -> Lê" (aqui: quadros de 8 bytes, não linhas). */
static void lerSerial()
{
    while (Serial.available() > 0)
    {
        Protocol::Frame quadro;

        if (!decoder.push(static_cast<uint8_t>(Serial.read()), quadro))
            continue;

        if (quadro.type != Protocol::CONTROLLER_STATE)
            continue; /* outros tipos são válidos, mas o braço só usa o estado do controle */

        ultimoRecebimento = millis();
        interpretaEstado(quadro.state());
    }

    /* Quadros quebrados: no máximo um log por segundo. */
    const unsigned long erros = decoder.errors() + decoder.discarded();

    if (erros != errosLogados && millis() - ultimoLogErro >= 1000)
    {
        Serial.printf("[ARM] quadros válidos=%lu, inválidos=%lu, bytes descartados=%lu\n",
                      decoder.frames(), decoder.errors(), decoder.discarded());
        errosLogados = erros;
        ultimoLogErro = millis();
    }
}

/*
 * "Para cada servo: direção 1 -> aumenta 1 grau; -1 -> diminui 1 grau; 0 -> mantém;
 *  limita ao mínimo e máximo; envia o novo ângulo para o servo."
 */
static bool atualizaAngulo(int &ang, int dir, int minimo, int maximo, uint8_t pino, uint8_t canal, bool inverte)
{
    if (dir == 0)
        return false;

    const int novo = constrain(ang + dir * PASSO_GRAUS, minimo, maximo);

    if (novo == ang)
        return false; /* já está no limite */

    ang = novo;
    servoEscrever(pino, canal, ang, inverte);
    return true;
}

void setup()
{
    Serial.begin(SERIAL_BAUD);

    /* Inicializa 4 servos, define pinos GPIO e posiciona na posição inicial. */
    servoIniciar(PINO_BASE, CANAL_BASE, angBase, INVERTE_BASE);
    servoIniciar(PINO_OMBRO, CANAL_OMBRO, angOmbro, INVERTE_OMBRO);
    servoIniciar(PINO_COTOVELO, CANAL_COTOVELO, angCotovelo, INVERTE_COTOVELO);
    servoIniciar(PINO_GARRA, CANAL_GARRA, angGarra, INVERTE_GARRA);

    delay(50);

    Serial.println();
    Serial.printf("[ARM] %s: aguardando quadros da Raspberry\n", FIRMWARE_VERSION);
    Serial.printf("[ARM] base     GPIO%-2u %3d..%3d inicial %3d\n", unsigned(PINO_BASE), MIN_BASE, MAX_BASE, angBase);
    Serial.printf("[ARM] ombro    GPIO%-2u %3d..%3d inicial %3d\n", unsigned(PINO_OMBRO), MIN_OMBRO, MAX_OMBRO, angOmbro);
    Serial.printf("[ARM] cotovelo GPIO%-2u %3d..%3d inicial %3d\n", unsigned(PINO_COTOVELO), MIN_COTOVELO, MAX_COTOVELO,
                  angCotovelo);
    Serial.printf("[ARM] garra    GPIO%-2u %3d..%3d inicial %3d\n", unsigned(PINO_GARRA), MIN_GARRA, MAX_GARRA, angGarra);

    ultimoMovimento = millis();
}

void loop()
{
    /* Dados seriais disponíveis? -> lê e interpreta. */
    lerSerial();

    /* Tratamento de falhas: comunicação serial parada por mais de 250 ms -> zera as direções. */
    if (estado != ArmMapping::ALL_RELEASED && millis() - ultimoRecebimento > TIMEOUT_SERIAL_MS)
    {
        Serial.printf("[ARM] %lu ms sem dados seriais: direções zeradas\n", TIMEOUT_SERIAL_MS);
        interpretaEstado(ArmMapping::ALL_RELEASED);
    }

    /* Tempo para atualizar servos (>= 25 ms)? */
    if (millis() - ultimoMovimento < INTERVALO_MS)
        return;

    ultimoMovimento = millis();

    /* Atualiza ângulos dos servos. */
    bool mudou = atualizaAngulo(angBase, dirBase, MIN_BASE, MAX_BASE, PINO_BASE, CANAL_BASE, INVERTE_BASE);
    mudou |= atualizaAngulo(angOmbro, dirOmbro, MIN_OMBRO, MAX_OMBRO, PINO_OMBRO, CANAL_OMBRO, INVERTE_OMBRO);
    mudou |= atualizaAngulo(angCotovelo, dirCotovelo, MIN_COTOVELO, MAX_COTOVELO, PINO_COTOVELO, CANAL_COTOVELO,
                            INVERTE_COTOVELO);
    mudou |= atualizaAngulo(angGarra, dirGarra, MIN_GARRA, MAX_GARRA, PINO_GARRA, CANAL_GARRA, INVERTE_GARRA);

    /* Log só quando o movimento termina (soltou o botão ou chegou no limite). */
    if (movendo && !mudou)
        Serial.printf("[ARM] parado: angBase=%d angOmbro=%d angCotovelo=%d angGarra=%d\n",
                      angBase, angOmbro, angCotovelo, angGarra);

    movendo = mudou;
}
