# SNES-BT — pacote de implementação inicial

Esta entrega amplia o firmware para ESP32 DevKit V1 e adiciona um receptor Linux de referência.

## Arquivos
- Firmware Arduino: `.ino`, GPIO, engine SNES, BLE, UART2, encoder CRC e seleção de transporte.
- `receiver/snes_uinput_daemon.cpp`: parser de frames de 8 bytes, CRC, UART + Unix socket, `uinput`, watchdog de 40 ms e liberação de teclas.
- `receiver/android/ProtocolFrame.java`: parser incremental para integrar no callback BLE do APK.
- `receiver/README.md`: como integrar a camada Android e compilar/executar o daemon.

## Pinout adotado
Preservei os GPIOs que estavam no firmware fornecido. UART2 usa GPIO17 TX e GPIO16 RX, livres no pinout atual. Não alterei o mapeamento dos botões/SNES.

**Atenção elétrica:** GPIO34 e GPIO35 são entradas somente e não têm pull-up interno. Como o firmware lê botões ativos em LOW, coloque resistores externos de pull-up (por exemplo 10 kΩ para 3,3 V) nesses dois sinais, caso ainda não existam no circuito. GPIO12 é pino de strapping do ESP32; valide que o circuito conectado a DATA não força nível inadequado durante o boot.

## Estado honesto da implementação
- O daemon Linux foi compilado neste ambiente com `g++ -std=c++17 -Wall -Wextra -Werror`.
- Não consegui compilar o sketch Arduino aqui: `arduino-cli` não está instalado neste ambiente.
- O APK existente não foi reescrito, porque o `MainActivity.java` presente entre os arquivos fornecidos é apenas uma Activity de interface; a implementação BLE/LocalSocket descrita nas notas não está nesse arquivo. Incluí a classe de parser e o ponto de integração documentado, para aplicar na classe GATT real sem destruir o código funcional.
- A alternância SELECT curto BLE/WIRED está implementada no ESP32. A negociação com ACK/READY não está integrada ainda; o receptor usa watchdog de 40 ms. Antes de considerar a troca fail-safe completa, é necessário integrar a confirmação bilateral ao APK e testar os dois transportes no hardware.
- SELECT longo é detectado, mas o modo acelerômetro não é ativado: faltam confirmação dos GPIOs analógicos do ADXL335B e calibração. A garra também depende do mecanismo e dos thresholds físicos.
- A temporização SNES continua sendo a versão existente com delays, não a versão final baseada em periférico/timer.

## Sequência recomendada de validação
1. Compile o firmware com Arduino IDE / ESP32 core 2.x ou 3.x e corrija eventuais diferenças de API BLE da versão instalada.
2. Teste BLE sozinho e confirme que a notificação agora tem 8 bytes, não 2.
3. Integre `ProtocolFrame.java` ao callback GATT atual e encaminhe os 8 bytes pelo LocalSocket.
4. Compile/deploy o daemon ARM64 com NDK no Android e confirme permissões de `/dev/uinput`.
5. Configure a UART do Raspberry/GloDroid, conecte TX/RX cruzados e GND, e teste o modo WIRED.
6. Teste perda de frames, desconexão BLE e watchdog com botão pressionado.


## Mapeamento do acelerometro e debug
No modo ACCELEROMETER: inclinar para cima/levantar -> X; para baixo/abaixar -> B; direita -> A; esquerda -> Y. O Monitor Serial (115200 baud) mostra `[ACCEL->BUTTON] <botao> PRESSIONADO/SOLTO` apenas quando o estado muda, incluindo o delta ADC. O limiar inicial e 520 contagens ADC e pode precisar de ajuste.
