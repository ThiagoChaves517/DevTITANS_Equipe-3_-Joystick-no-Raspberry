# Operação e diagnóstico: `snes_arm_daemon`

## Implantar (desenvolvimento)

```sh
braco/scripts/deploy.sh                    # compila, envia para /data/local/tmp e inicia (--device auto)
DEVICE=usb:1-1.3 braco/scripts/deploy.sh   # fixa a serial do braço pela porta USB física
braco/scripts/deploy.sh stop
```

O lado do joystick (app + `snes_input_daemon`) sobe com o `scripts/deploy.sh` da raiz (branch driver).

## Testes sem hardware

```sh
braco/daemon/test.sh                        # quadro, mapeamento, contrato com o snes_input_daemon, política de envio
braco/firmware_esp32/host_test/run.sh       # roda o firmware do braço no computador (Arduino.h simulado)
```

## Teste de integração no Raspberry (sem joystick e sem ESP32)

```sh
python3 braco/scripts/device_test.py
```

O script sobe uma instância de teste do `snes_input_daemon`, com socket e nome próprios para não
interferir no app real. Ele usa o `ipc_probe` do Raphael para mandar estados e um `frame_sniffer`
fazendo o papel do ESP32. Depois confere a sequência de 16 bits que chegou ao "braço", o keepalive e o
neutro quando o gamepad some.

## Controle manual

```sh
adb shell /data/local/tmp/snes_arm_daemon --list                     # seriais USB vistas pela placa
adb shell /data/local/tmp/snes_arm_daemon --device /dev/ttyUSB1 --echo --verbose
adb shell "nohup /data/local/tmp/snes_arm_daemon > /data/local/tmp/snes_arm_daemon.log 2>&1 &"
```

| Opção | Padrão | Para quê |
|---|---|---|
| `--device` | `auto` | `auto`, `/dev/ttyUSBn`, `usb:<porta>` ou `tcp:HOST:PORTA` (simulador) |
| `--name` | `SNES-BT-Controller` | nome do gamepad do `snes_input_daemon` |
| `--input` | — | usar um `/dev/input/eventN` fixo em vez de procurar pelo nome |
| `--keepalive-ms` | 100 | reenvio enquanto há botão pressionado (máx. 200: o ESP32 para em 250) |
| `--boot-wait-ms` | 1500 | espera o boot do ESP32 depois de abrir a serial |
| `--echo` | — | mostra no log o que o ESP32 escreve (`[ARM] ...`) |
| `--verbose` | — | loga cada mudança de estado |

## Sem joystick e sem braço

```sh
adb shell /data/local/tmp/snes_fake_pad                       # gamepad falso: press A / hold B 500 / hat x -1 / state FEFF
adb shell /data/local/tmp/frame_sniffer                       # "ESP32 falso": imprime PTY /dev/pts/N
adb shell /data/local/tmp/snes_arm_daemon --device /dev/pts/N --boot-wait-ms 0
```

Simulador visual no computador:

```sh
python3 braco/simulador/arm_sim.py      # painel em http://127.0.0.1:8787
adb reverse tcp:7878 tcp:7878
adb shell /data/local/tmp/snes_arm_daemon --device tcp:127.0.0.1:7878
```

## Diagnóstico

```sh
adb shell tail -f /data/local/tmp/snes_arm_daemon.log
adb logcat -s SnesArmDaemon SnesDaemon        # este daemon e o do Raphael
adb shell getevent -lt                        # eventos do gamepad (o que o snes_arm_daemon lê)
adb shell ls -l /dev/ttyUSB* /dev/ttyACM*
```

| Log | Significado |
|---|---|
| `aguardando o controle "SNES-BT-Controller"` | o `snes_input_daemon` não está rodando |
| `mais de uma serial USB; escolha a do braço com --device` | dois ESP32 na USB: use `--list` e `--device usb:<porta>` |
| `nenhuma serial USB` | ESP32 desligado, cabo só de carga, ou placa com CH340 (o kernel não tem `ch341`) |
| `link com o braço perdido` | cabo removido. O ESP32 para sozinho em 250 ms, e o daemon reconecta a cada 1 s |
| `controle perdido: enviando estado neutro ao braço` | o gamepad sumiu. O braço recebe `0xFFFF` na hora |
