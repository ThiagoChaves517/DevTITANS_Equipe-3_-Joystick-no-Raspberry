# Tutorial rápido: controlar o braço pela Raspberry

Resumo prático. Os detalhes estão em [README.md](README.md) e o protocolo em [PROTOCOLO.md](PROTOCOLO.md).

## 1. O que precisa

| Item | Para quê |
|---|---|
| Raspberry Pi 4 com GloDroid (build `userdebug`) + cabo USB-C no computador | roda a bridge; `adb root` precisa funcionar |
| Braço MeArm com 4 servos SG90 | o braço |
| **Fonte 5 V externa, ≥ 2 A** | alimentar os servos (**nunca** pela Raspberry ou pelo ESP32) |
| ESP32 DevKit com chip **CP2102** (ou ESP32-S2/S3 com USB nativo) + cabo USB | controla os servos (caminho recomendado). **CH340 não funciona** |
| Joystick + APK + `snes_uinput_daemon` (Equipe Joystick) | opcional: dá para testar com o controle falso |

No computador:
- `adb` (Android platform-tools)
- **Android NDK 27.0.12077973**, que é o mesmo da wiki do driver. Pelo Android Studio: *SDK Manager › SDK Tools › NDK*. Ou: `sdkmanager "ndk;27.0.12077973"` (~2,4 GB)
- **Arduino IDE** com o core "esp32" da Espressif, para gravar o ESP32
- **Python 3**, para o simulador. Não precisa de nenhuma biblioteca extra

## 2. Instalar (uma vez)

```bash
git clone git@github.com:ThiagoChaves517/DevTITANS_Equipe-3_-Joystick-no-Raspberry.git
```

```bash
cd DevTITANS_Equipe-3_-Joystick-no-Raspberry && git checkout integracao-braco
```

Compile e envie para a Raspberry. O script descobre o NDK sozinho, ou você pode passar `NDK=/caminho`:

```bash
braco/bridge/build.sh deploy
```

Isso coloca `snes_arm_bridge` e `snes_fake_pad` em `/data/local/tmp/`.

## 3. Montar e gravar o ESP32

1. Ligue os servos. O sinal (fio laranja) vai assim: base no **GPIO25**, ombro no **GPIO26**, cotovelo no **GPIO27** e garra no **GPIO13**.
   O vermelho vai no **+5 V da fonte**, e o marrom no **GND da fonte**, que também precisa ir ao **GND do ESP32**.
2. Na Arduino IDE, abra `braco/firmware_esp32/SNES_ARM_ESP32/SNES_ARM_ESP32.ino`, escolha a placa **ESP32 Dev Module** e grave.
3. Teste sozinho no Monitor Serial (115200, "Nova linha"). Deve aparecer `READY SNES-ARM-ESP32 1.0`. Digite `J 90 90 90 60` e o braço vai para home.
4. **Calibre** antes de usar de verdade (README › Calibração): mova cada junta de 5 em 5° e anote onde encosta em `ArmConfig.h`.
5. Ligue o ESP32 numa porta USB-A da Raspberry.

## 4. Usar

**Com tudo ligado.** Primeiro suba o joystick, seguindo a wiki "Comandos para executar a aplicação ‐ Driver". Depois:

```bash
adb shell /data/local/tmp/snes_arm_bridge
```

O esperado é `[SERIAL] conectado em /dev/ttyUSB0`, depois `firmware pronto`, e por fim `braço pronto: comandos liberados`.

**Sem joystick (controle falso):**

```bash
adb push braco/testes/roteiro_basico.txt /data/local/tmp/
```

```bash
adb shell "cd /data/local/tmp && (./snes_fake_pad < roteiro_basico.txt &) ; ./snes_arm_bridge"
```

**Sem braço (simulador no computador).** São três terminais:

```bash
python3 braco/simulador/arm_sim.py
```

```bash
adb reverse tcp:7878 tcp:7878
```

```bash
adb shell /data/local/tmp/snes_arm_bridge --backend tcp
```

Abra http://127.0.0.1:8787 para ver o braço se mexer.

**Sem ESP32 (servos direto na Raspberry).** O sinal vai nos GPIO 17/27/22/23 (pinos 11/13/15/16), com o GND da fonte no pino 14:

```bash
adb shell /data/local/tmp/snes_arm_bridge --backend gpio
```

Para parar: `Ctrl+C`. A bridge manda `S` e o braço congela.

## 5. Controles

| | Página 1 (inicial) | Página 2 |
|---|---|---|
| B / Y | cotovelo sobe / desce | ombro sobe / desce |
| A / X | abre / fecha a garra | base + / − |
| START curto | troca de página | troca de página |
| START 1,5 s | home | home |

A junta só anda enquanto o botão está apertado. O D-pad, se existir, sempre move a base e o ombro.
O SELECT é do joystick: curto troca BLE/fio, segurado 5 s liga o acelerômetro.

## 6. Deu problema?

| Sintoma | O que fazer |
|---|---|
| `nenhuma serial encontrada` | Rode `adb shell ls /dev/ttyUSB* /dev/ttyACM*`. Se não aparecer nada, o ESP32 provavelmente tem **CH340**: troque a placa ou veja `aosp/README.md` |
| `não respondeu READY/OK` | O firmware não está gravado, ou essa porta é outro dispositivo, como o ESP32 do joystick |
| `procurando controle SNES-BT-Controller` não sai disso | O `snes_uinput_daemon` não está rodando. Teste com o `snes_fake_pad` |
| `Permission denied` em `/dev/...` | Rode `adb root` antes |
| Os botões não mexem nada | Confira a página (START troca) e se a junta já não está no limite |
| Servo treme ou a placa reinicia | A fonte é fraca ou falta o GND comum. No modo `gpio`, algum tremor é normal (PWM por software) |
| Junta anda ao contrário | Em `ArmConfig.h`, mude `inverted` da junta para `true` |
| `# WATCHDOG` no Monitor Serial | É normal quando a bridge para: o braço congela por segurança |

## 7. Limpar depois

```bash
adb shell rm /data/local/tmp/snes_arm_bridge /data/local/tmp/snes_fake_pad /data/local/tmp/roteiro_basico.txt
```

```bash
adb reverse --remove-all
```
