# Tutorial rápido: braço controlado pela Raspberry

Resumo prático. Os detalhes estão em [README.md](README.md), [docs/OPERACAO.md](docs/OPERACAO.md) e
[docs/protocolo_serial.md](docs/protocolo_serial.md).

## 1. O que precisa

| Item | Para quê |
|---|---|
| Raspberry Pi 4 com GloDroid (build `userdebug`) + cabo USB-C no computador | roda os dois drivers; `adb root` precisa funcionar |
| Joystick + app + `snes_input_daemon` (Equipe Joystick, branch `driver`) | gera o gamepad `SNES-BT-Controller` que o `snes_arm_daemon` lê |
| ESP32 do braço com chip **CP2102** (ou USB nativo) + cabo USB de dados | recebe os 16 bits e move os servos. **CH340 não funciona** |
| Braço com 4 servos SG90 + **fonte 5 V externa (3–5 A)** | o braço. GND da fonte ligado ao GND do ESP32 |

No computador:
- `adb`
- **Android NDK** (o 27.0.12077973 é o da wiki do driver). Se houver mais de um, defina `ANDROID_NDK_HOME`
- **Arduino IDE** com o core "esp32" da Espressif
- **Python 3**, só para o simulador e o teste integrado

## 2. Instalar (uma vez)

```bash
git clone git@github.com:ThiagoChaves517/DevTITANS_Equipe-3_-Joystick-no-Raspberry.git
```

```bash
cd DevTITANS_Equipe-3_-Joystick-no-Raspberry && git checkout integracao-braco
```

```bash
braco/daemon/test.sh
```

## 3. Gravar o ESP32 do braço

1. Ligue os servos: base no **GPIO25**, ombro no **26**, cotovelo no **27** e garra no **12**. O vermelho vai no +5 V da fonte, e o marrom no GND da fonte e do ESP32.
2. Na Arduino IDE, abra `braco/firmware_esp32/SNES_ARM_ESP32/SNES_ARM_ESP32.ino`, escolha a placa **ESP32 Dev Module** e grave.
3. No Monitor Serial (115200), deve aparecer `[ARM] SNES-ARM-ESP32 2.1: aguardando quadros da Raspberry` e os servos vão para 90/90/90/60.
4. **Calibre** os limites de `ArmConfig.h` com o braço montado. Os valores atuais são o exemplo do fluxograma do William.
5. Ligue o ESP32 numa USB-A da Raspberry.

## 4. Usar

**4.1. Suba o joystick** (Equipe Joystick), na raiz do repositório:

```bash
scripts/deploy.sh
```

**4.2. Suba o driver do braço:**

```bash
braco/scripts/deploy.sh
```

No log, o esperado é `controle encontrado`, depois `conectado ao braço em /dev/ttyUSB0`, e por fim `braço pronto`.

**4.3. Se o ESP32 do joystick também estiver na USB** (como no diagrama do William), **sempre** fixe a porta do braço pelo número da porta física. O `auto` só funciona com uma serial; se o braço estiver desligado, ele pegaria a do joystick e a reiniciaria.

Veja as portas e, depois, suba fixando a do braço:

```bash
adb shell /data/local/tmp/snes_arm_daemon --list
```

```bash
DEVICE=usb:1-1.3 braco/scripts/deploy.sh
```

**4.4. Para parar:**

```bash
braco/scripts/deploy.sh stop
```

O daemon manda "solta tudo" ao braço antes de sair.

## 5. Testar sem o hardware completo

**Teste automático na Raspberry** (sem joystick e sem ESP32). Ele usa o daemon real do Raphael e um ESP32 falso:

```bash
python3 braco/scripts/device_test.py
```

**Simulador visual** no computador, em três terminais:

```bash
python3 braco/simulador/arm_sim.py
```

```bash
adb reverse tcp:7878 tcp:7878
```

```bash
adb shell /data/local/tmp/snes_arm_daemon --device tcp:127.0.0.1:7878
```

Abra http://127.0.0.1:8787 e use o joystick ou o `snes_fake_pad`.

**Firmware do braço sem ESP32**, no computador:

```bash
braco/firmware_esp32/host_test/run.sh
```

Depois digite, por exemplo, `state FEFF` (A pressionado), `@500` e `state FFFF`.

## 6. Controles (wiki do William)

| Botão | Braço |
|---|---|
| ← / → (analógico ou inclinação) | gira a base |
| ↑ / ↓ (analógico ou inclinação) | sobe / desce o ombro |
| B / Y | sobe / desce o cotovelo |
| A / X | abre / fecha a garra |
| Start | para tudo enquanto pressionado |
| Select | no joystick: curto troca BLE/fio, 5 s liga o acelerômetro |

## 7. Deu problema?

| Sintoma no log | O que fazer |
|---|---|
| `aguardando o controle "SNES-BT-Controller"` | O `snes_input_daemon` não está rodando: suba o lado do joystick |
| `nenhuma serial USB` | Rode `adb shell ls /dev/ttyUSB* /dev/ttyACM*`. Se não aparecer nada, a placa provavelmente tem CH340 (sem driver no kernel) ou o cabo é só de carga |
| `mais de uma serial USB` | Dois ESP32 na USB: use `--list` e `DEVICE=usb:<porta>` |
| `abrir /dev/ttyUSB0: Permission denied` | Rode `adb root` antes |
| Servo não mexe, mas o log está ok | Rode com `--echo` para ver o `[ARM]` do ESP32. Confira a fonte 5 V, o GND comum e se a junta já está no limite |
| ESP32 não liga com a garra conectada | GPIO12 é *strapping*: solte o fio da garra no boot |
| Junta anda ao contrário | Em `ArmConfig.h`, mude `INVERTE_<JUNTA>` (ex.: `INVERTE_BASE`) para `true` |

## 8. Limpar depois

```bash
braco/scripts/deploy.sh stop
```

```bash
adb shell rm /data/local/tmp/snes_arm_daemon /data/local/tmp/frame_sniffer /data/local/tmp/snes_fake_pad /data/local/tmp/snes_arm_daemon.log
```
