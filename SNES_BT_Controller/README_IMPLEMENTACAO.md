# SNES-BT — firmware ESP32 DevKit V1 / etapa de transporte

## Implementado
- Frame de 8 bytes: `SN`, versão 1, tipo, sequência, payload little-endian e CRC-8/ATM (poly `0x07`, init `0x00`).
- BLE GATT envia notificações com o frame completo (não mais o estado cru de 2 bytes).
- UART2 ativa em 115200, 8N1: ESP32 TX=GPIO17 e RX=GPIO16; `Serial`/USB continua separado para logs.
- SELECT curto alterna BLE/WIRED; detecção por borda com debounce temporal básico.
- Pressão longa de SELECT (5 s) é detectada sem gerar repetidas ações; a mudança para acelerômetro não é ativada enquanto os pinos e calibração do ADXL335B não forem confirmados.
- GPIO34 e GPIO35 são somente entrada e não têm pull-up interno: exigem resistores externos de pull-up se os botões forem ativos em LOW.

## Fiação UART ESP32 DevKit V1 ↔ Raspberry Pi
- ESP32 GPIO17 / TX2 → Raspberry Pi RXD (normalmente GPIO15, pino físico 10).
- ESP32 GPIO16 / RX2 ← Raspberry Pi TXD (normalmente GPIO14, pino físico 8).
- GND ESP32 ↔ GND Raspberry Pi.
- Níveis UART 3,3 V. Não conecte o pino 5 V do Raspberry Pi aos GPIOs do ESP32. Confirme o nome real do dispositivo serial no sistema Android/Linux (ex.: `/dev/ttyS0`, `/dev/ttyAMA0` ou outro).

## Importante sobre a troca de transporte
Esta versão alterna localmente o transmissor com SELECT. O handshake `TRANSPORT_SWITCH/ACK/READY` ainda não foi integrado ao APK; portanto, o receptor deve aceitar os frames vindos do transporte atualmente ativo e aplicar o watchdog de 40 ms. O modo wired precisa ser validado no hardware antes de ser usado como modo principal.

## Limitações ainda abertas
- A aplicação Android existente deve ser atualizada para validar e encaminhar frames de 8 bytes. O código `MainActivity.java` disponível nesta entrega é apenas uma Activity de layout e não contém o cliente BLE/LocalSocket descrito nas notas do projeto; não foi sobrescrito.
- O daemon receptor com parser/CRC/watchdog/uinput está em `receiver/snes_uinput_daemon.cpp`.
- A implementação de temporização SNES continua sendo a implementação existente por delays; não é temporização definitiva por periférico de hardware.
- ADXL335B e garra não podem ser concluídos sem o mapeamento físico/calibração.
- Compilação Arduino não foi executada neste ambiente porque `arduino-cli` não está instalado. O daemon C++ é compilado separadamente com `g++` no Linux para validação local.
