# Correcao do SELECT e diagnostico do GY-61

## Mudancas
- SELECT: debounce estavel de 40 ms, pressao curta na soltura alterna BLE/WIRED; >=5000 ms alterna BUTTON/ACCELEROMETER imediatamente e apenas uma vez por pressionamento.
- Log `[STATUS]` a cada 500 ms com modo, transporte e estado bruto do SELECT.
- Log `[ACCEL]` a cada 500 ms quando em modo ACCELEROMETER, mostrando valores filtrados e deltas relativos a calibracao.
- Em BUTTON nao imprime continuamente X/Y/Z porque o acelerometro nao e amostrado nesse modo.

## Teste
1. Abra o Monitor Serial a 115200 baud.
2. Sem tocar em SELECT, observe `[STATUS] Entrada=BUTTON Transporte=BLE SELECT=SOLTO`.
3. Pressione e mantenha SELECT. Deve mostrar `SELECT=PRESSIONADO` e, apos 5 s, `[INPUT] Modo: ACCELEROMETER`.
4. Solte: NAO deve alternar transporte.
5. Incline o modulo: observe `dX`, `dY`, `dZ` mudando.
6. Pressione SELECT rapidamente: deve alternar BLE/WIRED, sem trocar o modo de entrada.
7. Se `SELECT=PRESSIONADO` aparecer sem apertar, revise o GPIO25, o fio e o GND do botao. O botao deve ligar GPIO25 a GND ao ser pressionado (INPUT_PULLUP).

## Pinagem da versao
GY-61 X=GPIO36, Y=GPIO39, Z=GPIO33, VCC=3V3, GND=GND.
Botao SELECT=GPIO25; botao B=GPIO18 (MOVIDO do GPIO33); Y=GPIO35 e X=GPIO34 precisam de resistores pull-up externos.

## Limitacoes
Nao foi testado em hardware nem compilado com o core ESP32 3.3.7 neste ambiente. Nao implementa handshake bilateral BLE/UART nem a integracao final do receptor GloDroid.
