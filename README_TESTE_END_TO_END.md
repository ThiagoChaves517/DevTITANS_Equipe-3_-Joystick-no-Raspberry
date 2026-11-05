# SNES-BT — pacote de teste integrado

Este pacote junta o aplicativo Android BLE, o cliente LocalSocket, o daemon Linux/uinput e o firmware para ESP32 DevKit V1.

## Estado real da implementação

- App Android: procura `SNES-BT-Controller`, conecta ao GATT, ativa notificações, valida frames de 8 bytes (SYNC, versão, tipo e CRC-8) e encaminha o frame completo ao daemon via LocalSocket.
- Daemon: aceita frames pelo LocalSocket e UART, valida o framing/CRC, atualiza `uinput` e libera os botões após 40 ms sem frame válido.
- Firmware: gera frames v1.0 e envia pelo BLE ou UART2. UART2 usa GPIO17 TX e GPIO16 RX a 115200 baud.
- A alternância por SELECT é local no firmware; o handshake bilateral ACK/READY ainda não está implementado. A alternância acelerômetro também não está funcional.
- A integração do daemon com init/SELinux do GloDroid depende da imagem AOSP e das permissões reais da placa.

## A. Compilar o aplicativo Android

1. Abra a pasta `ble` no Android Studio.
2. Aguarde a sincronização do Gradle com rede disponível.
3. Execute `Build > Make Project`.
4. Instale o APK no Android/GloDroid que será o receptor BLE.

O build não foi executado neste ambiente porque o Gradle Wrapper tentou baixar Gradle 8.11.1 e o ambiente não tinha acesso DNS à internet. Não interprete isso como build aprovado.

## B. Compilar o firmware ESP32

1. Abra `firmware/SNES_BT_Controller/SNES_BT_Controller.ino` no Arduino IDE.
2. Selecione a placa **ESP32 Dev Module** (perfil usual do ESP32 DevKit V1) e a versão instalada do pacote ESP32.
3. Confirme que a biblioteca BLE da plataforma ESP32 está disponível.
4. Compile antes de gravar; o build do firmware ainda não foi validado neste ambiente.

## C. Compilar o daemon no Linux alvo

O daemon precisa ser compilado para a arquitetura do sistema alvo (não use um binário x86_64 em uma Raspberry ARM).

```sh
cd receiver
./build_daemon.sh
```

O daemon requer acesso a `/dev/uinput` e cria `/data/local/tmp/snes_uinput.sock` por padrão. Se o processo for executado como serviço Android, configure permissões e SELinux na imagem AOSP; `chmod 777` não é uma solução de produção.

Para testar outro caminho de socket e UART:

```sh
./snes_uinput_daemon /data/local/tmp/snes_uinput.sock /dev/ttyS0
```

`/dev/ttyS0` é apenas um padrão. Identifique o UART real no seu GloDroid antes de usar. A UART deve estar configurada em 115200 8N1, sem console de boot/logs compartilhando a mesma linha.

## D. Ligação UART

- ESP32 GPIO17 TX2 -> RX da Raspberry Pi.
- ESP32 GPIO16 RX2 <- TX da Raspberry Pi (necessário quando o firmware implementar respostas; no fluxo atual de estados o ESP32 envia frames).
- ESP32 GND -> GND da Raspberry Pi.
- Sinais UART de 3,3 V; não conecte TX de 5 V diretamente ao ESP32.

O GPIO16/17 no ESP32 DevKit V1 são os pinos escolhidos pelo firmware. Confirme que não estão ocupados por periféricos na sua placa.

## E. Ordem de testes

1. **Build do app:** compilar no Android Studio. Corrigir qualquer erro antes de prosseguir.
2. **Build do daemon:** compilar no Raspberry/GloDroid ou cross-compilar para a arquitetura correta.
3. **Permissões do daemon:** verificar `/dev/uinput`, criação do socket e ausência de erros SELinux.
4. **Dispositivo virtual:** iniciar daemon e confirmar `SNES-BT-Controller` em `/dev/input` ou em `adb shell getevent -lp`.
5. **BLE sem botões:** ligar ESP32 e confirmar que o app encontra e conecta ao `SNES-BT-Controller`; deve aparecer CRC OK.
6. **Botões individuais:** pressionar A, B, X, Y e START um por vez; confirmar os eventos no `getevent`/emulador. SELECT curto tem função de alternância, portanto testar separado.
7. **Integridade de frame:** confirmar que frames inválidos/CRC errado são descartados; não deve haver eventos falsos.
8. **Watchdog:** manter um botão pressionado e desligar o ESP32/desconectar BLE; o daemon deve liberar todos os botões em até aproximadamente 40 ms após o último frame válido.
9. **UART:** desligar o controle, conectar TX/RX/GND, configurar o dispositivo serial correto e iniciar daemon com o caminho UART real. Confirmar frames recebidos.
10. **Alternância SELECT:** testar pressão curta várias vezes e confirmar que o ESP32 passa a transmitir somente pelo transporte selecionado. Sem handshake ACK/READY, considerar este teste funcional preliminar, não validação de segurança.
11. **Reconexão:** desligar e religar o ESP32; app deve voltar a procurar e conectar sem reinício manual.
12. **Teste prolongado:** manter uso por 10 minutos, observar desconexões, latência, CPU, mensagens de log e falsos acionamentos.

## F. Comandos úteis

```sh
adb shell ls -l /dev/uinput
adb shell ls -l /data/local/tmp/snes_uinput.sock
adb shell getevent -lp
adb logcat -s SnesBtReceiver SnesDaemonClient
```

O caminho de `getevent`, a disponibilidade de `/dev/uinput` e o nome do dispositivo serial podem variar conforme a imagem GloDroid.


## Atualizacao ADXL335 v1.3

O firmware atualizado esta em `firmware/SNES_BT_Controller`. Consulte `README_ADXL335_IMPLEMENTACAO.md` para a pinagem revisada e as limitacoes. O botao B mudou de GPIO33 para GPIO18; nao use a pinagem antiga sem recabear. O modo acelerometro usa X=GPIO36, Y=GPIO39, Z=GPIO33. O firmware nao foi compilado nem validado em hardware nesta sessao.
