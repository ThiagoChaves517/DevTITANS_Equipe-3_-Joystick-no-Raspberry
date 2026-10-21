# Firmware SNES-BT com GY-61 / ADXL335 (implementacao inicial)

## Pinagem alterada (obrigatoria para esta versao)
- GY-61 VCC -> 3V3; GND -> GND; X -> GPIO36; Y -> GPIO39; Z -> GPIO33.
- Botao B foi movido de GPIO33 para GPIO18 para liberar GPIO33 ao eixo Z.
- Os demais botoes e sinais SNES permanecem conforme `BoardConfig.h`.
- GPIO34 (X do botao) e GPIO35 (Y do botao) sao somente entrada e nao possuem pull-up interno; mantenha pull-ups externos no circuito.
- Confira todos os fios antes de alimentar. Nunca aplique 5 V nas entradas ADC do ESP32.

## Operacao
- Ao ligar, mantenha o GY-61 parado na posicao neutra durante a calibracao inicial (aprox. 0,5 s).
- Pressao curta de SELECT: alterna transporte BLE/UART localmente (handshake bilateral ainda nao implementado).
- Pressao longa de SELECT (5 s): alterna BUTTON/ACCELEROMETER uma vez por pressao.
- No modo ACCELEROMETER: levantar/inclinar para cima aciona X; abaixar/inclinar para baixo aciona B; direita aciona A; esquerda aciona Y. O firmware imprime eventos `[ACCEL->BUTTON]` ao pressionar/soltar, com deltas ADC.
- O quadro GRIP_STATE e emitido com 0=NEUTRAL, 1=OPEN, 2=CLOSE baseado no delta do eixo Z.

## Limites conhecidos
- Limiar de inclinacao (`TILT_THRESHOLD`) e grip (`GRIP_THRESHOLD`) sao valores ADC iniciais, precisam ser ajustados no hardware real.
- A orientacao dos eixos depende de como o GY-61 foi montado. Se os sentidos ficarem invertidos, troque os sinais de `dx`/`dy` nas quatro expressoes `mappedX`, `mappedB`, `mappedY` e `mappedA`.
- A calibracao e feita apenas no boot; nao ha recalibracao automatica.
- `GRIP_STATE` e enviado pelo firmware, mas o receptor Android/Linux precisa interpretar esse tipo para expor a funcao ao app.
- Handshake BLE/UART, autorizacao de transporte unico e testes de hardware ainda pendentes.
- Build Arduino nao foi executado neste ambiente; validar com Arduino IDE/arduino-cli e placa ESP32 Dev Module antes de usar.
