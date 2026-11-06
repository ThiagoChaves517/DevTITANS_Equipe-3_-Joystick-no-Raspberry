# Conformidade com a wiki do William

Referência: página **"Braço Robótico ‐ Semana 09\09‐21\10"** (William, 21/10, "não vai mudar"), as imagens que
ela mostra (`resources/Esquema de Controle Robótico com ESP32 e Raspberry Pi with acelerometro.png`) e o
fluxograma do firmware (`resources/Fluxograma de Controle de Servos ESP32.png`). Revisado em 05/11.

Legenda: ✅ segue · ⚠️ segue com ressalva · ❌ não segue (justificado)

## Arquitetura

| Wiki | Implementação | |
|---|---|---|
| ESP32 como interface adicional, controlando os 4 servos | `firmware_esp32/SNES_ARM_ESP32` num ESP32 só para o braço (o "ESP32 #2" do diagrama) | ✅ |
| Botões no protocolo SNES, cada botão um bit dos **16 bits** | A Raspberry repassa o registrador de 16 bits, sem mudar nada, dentro do quadro de 8 bytes do joystick (`docs/protocolo_serial.md`) | ✅ |
| Raspberry ↔ ESP32 #2 por **USB serial** (`/dev/ttyUSB1`, "a ordem pode variar") | `snes_arm_daemon --device auto / /dev/ttyUSBn / usb:<porta física>` | ✅ |
| ESP32 #1 (joystick) também na USB (`/dev/ttyUSB0`) | O daemon nunca abre a porta do joystick para testar. Com duas seriais, ele exige `--device` | ✅ |

## Mapeamento dos botões

| Botão (wiki) | Função (wiki) | Implementação (`ArmMapping.h`) | |
|---|---|---|---|
| ← / → | girar a base | `dirBase` = −1 / +1 | ✅ |
| ↑ / ↓ | subir e descer o ombro | `dirOmbro` = +1 / −1 | ✅ |
| B | subir o cotovelo | `dirCotovelo` = +1 | ✅ |
| Y | descer o cotovelo | `dirCotovelo` = −1 | ✅ |
| A | abrir a garra | `dirGarra` = +1 | ✅ |
| X | fechar a garra | `dirGarra` = −1 | ✅ |
| Select | selecionar o modo via acelerômetro | É feito no firmware do joystick (Select segurado 5 s); o braço não faz nada com ele | ✅ |
| Start | parar os movimentos | Todas as direções = 0 **enquanto Start estiver pressionado** (ADR-006) | ⚠️ A wiki não diz se é "enquanto pressiona" ou "trava até apertar de novo". Escolhi a leitura sem estado escondido. Confirmar com o William |
| "O mapeamento pode ser alterado conforme a ergonomia" | Tabela única em `ArmMapping.h` (firmware, testes e `frame_sniffer`), replicada no simulador | ✅ |
| "Base e ombro se movem enquanto o botão estiver pressionado" | Vale para as 4 juntas, como no item 5 de "Como o software funcionará" e no fluxograma | ✅ |

## Ligações

| Wiki | Implementação | |
|---|---|---|
| Base GPIO25, ombro GPIO26, cotovelo GPIO27, garra GPIO12 | `ArmConfig.h`: `PINO_BASE`… = 25/26/27/12 | ✅ |
| Sinal laranja/amarelo; V+ numa fonte **externa** de 5 V; GND comum | README, TUTORIAL e comentários. O firmware não depende disso | ✅ |
| Não alimentar os servos pelo 3,3 V do ESP32 nem pela Raspberry | Documentado | ✅ |
| ESP32 alimentado pela USB da Raspberry | É o próprio cabo de dados | ✅ |
| (implícito) GPIO12 | É pino de *strapping* do ESP32. Mantido, com aviso no `ArmConfig.h` e no README | ⚠️ Risco de a placa não ligar com a garra conectada. Validar no hardware |

## Como o software funcionará

| Passo (wiki) | Implementação | |
|---|---|---|
| 1. A Raspberry identifica os botões pressionados | `snes_arm_daemon` lê o gamepad do `snes_input_daemon` (evdev) e remonta os 16 bits | ✅ |
| 2. Envia os comandos ao ESP32 pela serial USB | 115200 8N1, na mudança + keepalive de 100 ms | ✅ |
| 3. O ESP32 interpreta cada comando | `FrameDecoder` (SYNC, versão, tipo, CRC) → `interpretaEstado()` | ✅ |
| 4. O servo se move em pequenos incrementos | `atualizaAngulo()`: 1° a cada 25 ms | ✅ |
| 5. Ao soltar o botão, o movimento para | A Raspberry envia o novo estado na hora. Se esse quadro se perder, o ESP32 para em 250 ms (sem keepalive) | ✅ |

## Fluxograma do firmware

| Fluxograma | `SNES_ARM_ESP32.ino` / `ArmConfig.h` | |
|---|---|---|
| Variáveis `angBase…angGarra`, `dirBase…dirGarra`, `MIN_xxx`/`MAX_xxx`, `ultimoMovimento`, `INTERVALO_MS` | Mesmos nomes | ✅ |
| Setup: Serial 115200, 4 servos, pinos, limites, posição inicial | `setup()` com `servoIniciar()` | ✅ |
| Limites de exemplo: base 0–180, ombro/cotovelo 20–160, garra 20–100 | `MIN_*`/`MAX_*` | ✅ (calibrar) |
| Ângulos iniciais 90/90/90/60 | `INICIAL_*` | ✅ |
| Loop: "Dados seriais disponíveis? → lê" | `lerSerial()` | ✅ |
| "Lê linha da Serial (até \n) e interpreta os 4 valores direcionais" | Lê **quadros de 8 bytes com os 16 bits** e calcula as 4 direções pela tabela | ❌ Pela própria wiki (16 bits) e pela escolha da equipe (quadro com CRC). O resultado é o mesmo: 4 direções −1/0/1 |
| "Tempo para atualizar servos (≥ 25 ms)?" → "Atualiza ângulos": +1, −1 ou mantém, limita, envia ao servo | `if (millis() - ultimoMovimento >= INTERVALO_MS)` → `atualizaAngulo()` | ✅ |
| Tratamento de falhas (opcional): mais de 250 ms sem dados → direções = 0 | Implementado (`TIMEOUT_SERIAL_MS = 250`) | ✅ |
| Pinos do quadro "Pinos dos servos": 13/14/25/26 | **25/26/27/12**, da tabela da wiki e do diagrama de ligações | ❌ O fluxograma diverge da página. Valem a página e o diagrama, que concordam entre si |

## O que a wiki ainda não cobre (e foi definido aqui)

- O formato do quadro na serial: o de 8 bytes do joystick (`docs/protocolo_serial.md`).
- A cadência na Raspberry: mudança + keepalive de 100 ms, menor que os 250 ms do watchdog do ESP32.
- A escolha da porta com dois ESP32 na USB: `--device usb:<porta física>`.
- "Código completo da ESP32: `empty`" na página. O código está em `braco/firmware_esp32/SNES_ARM_ESP32/` (a página do William não foi editada).

## Fora da parte do braço, mas diverge da imagem da wiki (Equipe Joystick)

| Imagem da wiki (ESP32 #1) | Firmware atual do joystick (branch `firmware`) |
|---|---|
| Acelerômetro **MPU6050** por I2C (SDA 22, SCL 21) | **ADXL335** analógico (GPIO 34/35/32) |
| D-pad com 4 botões (GPIO 32/33/25/26) | **Analógico** (potenciômetro, GPIO 25/26) gerando ↑↓←→ |
| B 4, Y 5, Select 16, Start 17, A 18, X 19 | B 4, Y 5, Select 13, Start 33, A 16, X 17 |
| Joystick ligado à Raspberry por **USB serial** | Joystick ligado por **BLE** (o app do Raphael) |

Para o braço nada disso importa: ele recebe os mesmos 16 bits, venha o controle por BLE ou por cabo. Mas a
imagem da wiki e o firmware do joystick deveriam ser alinhados pela equipe.
