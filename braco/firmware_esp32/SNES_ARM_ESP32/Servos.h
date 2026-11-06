#pragma once

#include <Arduino.h>

/*
 * PWM dos servos com o LEDC (hardware) do ESP32. Funciona no core 2.x (por canal)
 * e no 3.x (por pino); o canal só é usado no 2.x.
 */

/* "Inicializa servo / define pino GPIO / posiciona na posição inicial". */
void servoIniciar(uint8_t pino, uint8_t canal, int angulo, bool inverte);

/* "Envia o novo ângulo para o servo". */
void servoEscrever(uint8_t pino, uint8_t canal, int angulo, bool inverte);
