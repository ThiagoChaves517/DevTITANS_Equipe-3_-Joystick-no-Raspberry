//sudo chmod a+rw /dev/ttyUSB0sudo chmod a+rw /dev/ttyUSB0
#include <ESP32Servo.h>

Servo servoBase;
Servo servoOmbro;
Servo servoCotovelo;
Servo servoGarra;

// GPIOs dos quatro servos
const int PINO_BASE = 13;
const int PINO_OMBRO = 14;
const int PINO_COTOVELO = 18;
const int PINO_GARRA = 26;

// Limites iniciais: ajuste ao seu mecanismo
int angBase = 90;
int angOmbro = 90;
int angCotovelo = 90;
int angGarra = 60;

const int MIN_BASE = 0;
const int MAX_BASE = 180;

const int MIN_OMBRO = 20;
const int MAX_OMBRO = 160;

const int MIN_COTOVELO = 20;
const int MAX_COTOVELO = 160;

const int MIN_GARRA = 20;
const int MAX_GARRA = 100;

unsigned long ultimoMovimento = 0;
const unsigned long INTERVALO_MS = 25;

// Botoes do snes
#define BTN_B 0
#define BTN_Y 1
#define BTN_SELECT 2
#define BTN_START 3
#define BTN_UP 4
#define BTN_DOWN 5
#define BTN_LEFT 6
#define BTN_RIGHT 7
#define BTN_A 8
#define BTN_X 9
#define BTN_L 10
#define BTN_R 11


void pressButton(int bit) {

  switch (bit) {
      case BTN_B:
          // Código executado Subir o cotovelo
          Serial.println("BTN_B");
          atualizarServo(
            servoCotovelo, angCotovelo, 1,
            MIN_COTOVELO, MAX_COTOVELO
          );
          break;
      case BTN_Y:
          // Código executado Descer o cotovelo
          Serial.println("BTN_Y");
          atualizarServo(
            servoCotovelo, angCotovelo, -1,
            MIN_COTOVELO, MAX_COTOVELO
          );
          break;
      case BTN_UP:
          // Código executado Subir o ombro
          Serial.println("BTN_UP");
          atualizarServo(
            servoOmbro, angOmbro, 1,
            MIN_OMBRO, MAX_OMBRO
          );
          break;
      case BTN_DOWN:
          // Código executado descer o ombro
          Serial.println("BTN_DOWN");
          atualizarServo(
            servoOmbro, angOmbro, -1,
            MIN_OMBRO, MAX_OMBRO
          );
          break;
      case BTN_LEFT:
          // Código executado Girar a base esquerda
          Serial.println("BTN_LEFT");
          atualizarServo(
            servoBase, angBase, 1,
            MIN_BASE, MAX_BASE
          );
          break;
      case BTN_RIGHT:
          // Código executado Girar a base direita
          Serial.println("BTN_RIGHT");
          atualizarServo(
            servoBase, angBase, -1,
            MIN_BASE, MAX_BASE
          );
          break;
      case BTN_A:
          // Código executado Abrir a garra
          Serial.println("BTN_A");
          atualizarServo(
            servoGarra, angGarra, 1,
            MIN_GARRA, MAX_GARRA
          );
          break; 
      case BTN_X:
          // Código executado Fechar a garra
          Serial.println("BTN_X");
          atualizarServo(
            servoGarra, angGarra, -1,
            MIN_GARRA, MAX_GARRA
          );
          break;     
      default:
          // Código executado se nenhum dos casos anteriores for atendido
          Serial.println("default");
          break;
  }
}

void lerComando() {
 
 if (Serial.available() > 0) {
    int snesBit = Serial.parseInt(); // Lê o número diretamente da serial
    
    // O parseInt retorna 0 se falhar ou se receber 0. 
    // Para limpar o '\n' restante do buffer:
    if (Serial.peek() == '\n') {
      Serial.read(); 
    }
    
    if (snesBit > 0) {
         Serial.println(snesBit,HEX);

          //ler o bit para encontrar botão pressionado
          for (int i = 0; i < 16; i++) {
              // Desloca o bit 1 para a posição 'i' e faz um AND lógico
              if ((snesBit & (1 << i)) != 0) {
                  pressButton(i);
              }
          }
    }  
 }
 
}

void atualizarServo(
  Servo &servo, int &angulo, int direcao,
  int minimo, int maximo
) {
  if (direcao == 0) return;

  angulo += direcao;
  angulo = constrain(angulo, minimo, maximo);
  servo.write(angulo);
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(10);

  // Frequência típica dos SG90
  servoBase.setPeriodHertz(50);
  servoOmbro.setPeriodHertz(50);
  servoCotovelo.setPeriodHertz(50);
  servoGarra.setPeriodHertz(50);

  servoBase.attach(PINO_BASE, 500, 2400);
  servoOmbro.attach(PINO_OMBRO, 500, 2400);
  servoCotovelo.attach(PINO_COTOVELO, 500, 2400);
  servoGarra.attach(PINO_GARRA, 500, 2400);

  servoBase.write(angBase);
  servoOmbro.write(angOmbro);
  servoCotovelo.write(angCotovelo);
  servoGarra.write(angGarra);

  Serial.println("Olá, ESP32 inicializado!");
}

void loop() {
    
  unsigned long agora = millis();

  if (agora - ultimoMovimento >= INTERVALO_MS) {
    
    ultimoMovimento = agora;    
    
    lerComando();    

  }
}