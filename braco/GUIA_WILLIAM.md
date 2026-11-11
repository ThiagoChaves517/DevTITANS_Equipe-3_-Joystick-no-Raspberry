# Guia do braço para o William: Raspberry → seu firmware (06/11)

Tudo o que mudou em 06/11 e o que você precisa para ligar o **seu firmware** (`FirmawareBraco/firmwareBraco.ino`,
branch `FirmwareBraco`) na Raspberry. Este arquivo basta; os outros documentos de `braco/` são detalhe.

## 1. A cadeia

```
Joystick (ESP32, Bluetooth)
   │  firmware do Raphael (branch firmware)
   ▼
App com.snesbt.receiver ──IPC──▶ snes_input_daemon      ← driver do Raphael (branch driver)
                                     │ cria o gamepad "SNES-BT-Controller" (/dev/input/eventN)
                                     ▼
                               snes_arm_daemon            ← driver do braço (esta branch, braco/daemon)
                                     │ serial USB 115200, uma linha de texto a cada 30 ms
                                     ▼
                       ESP32 do braço: SEU firmwareBraco.ino  →  4 servos SG90
```

O `snes_arm_daemon` roda na Raspberry, lê o gamepad que o driver do Raphael cria e manda os botões para o
ESP32 do braço **no formato que o seu firmware já lê**. Seu `.ino` não precisa de nenhuma mudança.

## 2. O que mudou hoje (06/11)

| Mudança | Por quê |
|---|---|
| **`--protocolo william`, agora o padrão** | Seu firmware lê `Serial.parseInt()`, com bit 1 = pressionado. O driver mandava quadros binários de 8 bytes com bit 0 = pressionado, que só o `SNES_ARM_ESP32` entende. Hoje o driver manda exatamente o que seu `.ino` espera (seção 4). O formato antigo continua disponível em `--protocolo frame` |
| **Bug corrigido: 100% de CPU quando a serial cai** | Com o cabo do ESP32 arrancado ou o ESP32 desligado, o driver entrava em loop e não reconectava. Agora ele larga a serial, tenta de novo a cada 1 s e fica com menos de 1% de CPU |
| **Build e deploy funcionam no Windows (Git Bash)** | O `build.sh` acha o NDK do Android Studio em `%LOCALAPPDATA%\Android\Sdk` e o `deploy.sh` não quebra mais os caminhos do `adb` |
| **`.gitattributes`: scripts `.sh` sempre com LF** | No Windows, o git trazia os `.sh` com CRLF e o `sh` não os executava |
| **`snes_fake_pad` compila no NDK 30** | O `strtok` virou erro com `-Werror` |
| **`device_test.py` testa os dois protocolos** | Roda na Raspberry numa pasta própria (`/data/local/tmp/armtest`), sem sobrescrever o driver do Raphael |

## 3. O que precisa

| Item | Observação |
|---|---|
| Raspberry Pi 4 com GloDroid (Android 14, `userdebug`) | `adb shell su 0 id` precisa responder `uid=0` (root para o driver do braço) |
| App + `snes_input_daemon` do Raphael rodando | Já estão na placa. Confira com `adb shell ps -A \| grep snes` |
| **ESP32 do braço com chip USB CP2102** (ou USB nativo) | **CH340 não funciona**: o kernel da Raspberry não tem o driver, e a serial nem aparece |
| Cabo USB **de dados** do ESP32 até uma USB-A da Raspberry | Cabo só de carga não cria `/dev/ttyUSB0` |
| **Fonte 5 V externa (3–5 A) para os servos** | Os 4 SG90 puxam mais do que o ESP32 e a USB aguentam. O GND da fonte vai ligado ao GND do ESP32 |
| No computador: `adb`, **Android Studio com NDK** e Git Bash (no Windows) | Só para compilar e enviar o driver. Python 3 só para o teste automático |
| No seu computador: Arduino IDE com o core `esp32` e a biblioteca **ESP32Servo** | Para gravar o seu `.ino`, como você já faz |

Pinos do seu firmware (não mudaram): **base 13, ombro 14, cotovelo 18, garra 26**.

## 4. O que o seu firmware recebe

- Serial **115200 8N1**, só da Raspberry para o ESP32. O driver lê o que o ESP32 imprime e descarta; com
  `EXTRA="--echo"`, isso aparece no log.
- Cada mensagem é **uma linha**: o número decimal com os botões pressionados (bit 1 = pressionado) e `\n`.

| Botão | Bit | Valor | Seu firmware |
|---|---:|---:|---|
| B | 0 | 1 | cotovelo +1 |
| Y | 1 | 2 | cotovelo −1 |
| Select | 2 | 4 | nada (`default`) |
| Start | 3 | 8 | nada (`default`) |
| ↑ | 4 | 16 | ombro +1 |
| ↓ | 5 | 32 | ombro −1 |
| ← | 6 | 64 | base +1 |
| → | 7 | 128 | base −1 |
| A | 8 | 256 | garra +1 |
| X | 9 | 512 | garra −1 |
| L / R | 10 / 11 | 1024 / 2048 | nada |

Vários botões somam: `257` = A + B.

**Ritmo:**
- **Botão pressionado:** uma linha a cada **30 ms**, e nunca duas com menos de 30 ms, nem quando o botão muda. Seu
  `loop()` lê no máximo uma linha a cada 25 ms; se chegassem mais rápido, as linhas se acumulariam na serial
  e o braço continuaria andando depois de soltar. Na prática, são ~33 passos de 1° por segundo.
- **Nada pressionado:** **nenhuma linha.** O braço para porque as linhas param.
- **Controle desconectado ou driver do Raphael caiu:** as linhas param na hora.
- **Raspberry travou ou cabo saiu:** também não chega mais nada, e o braço para. Seu firmware não precisa de
  watchdog para isso, porque ele só anda quando recebe uma linha.
- Ao abrir a serial, o ESP32 DevKit reinicia (DTR/RTS). O driver espera **1,5 s** pelo boot antes da primeira linha.

Para mudar a velocidade sem mexer no firmware, use `--keepalive-ms` (mínimo 26). `EXTRA="--keepalive-ms 50"`
deixa o braço mais lento: 20°/s.

## 5. Instalar e rodar

### 5.1. Uma vez

```bash
git clone https://github.com/ThiagoChaves517/DevTITANS_Equipe-3_-Joystick-no-Raspberry.git
```

```bash
cd DevTITANS_Equipe-3_-Joystick-no-Raspberry && git checkout integracao-braco
```

No Windows, se o caminho da pasta for muito longo, clone com `git -c core.longpaths=true clone ...`.

### 5.2. Gravar o seu firmware

1. Na Arduino IDE, abra `FirmawareBraco/firmwareBraco.ino` (branch `FirmwareBraco`), escolha **ESP32 Dev
   Module** e grave.
2. No Monitor Serial (115200), deve aparecer `Olá, ESP32 inicializado!`. Digite `256` e Enter: a garra anda 1°.
3. **Feche o Monitor Serial** e ligue o ESP32 numa USB-A da Raspberry.

### 5.3. Subir o driver do braço

Na raiz do repositório (no Windows, no **Git Bash**):

```bash
EXTRA="--verbose" sh braco/scripts/deploy.sh
```

O script compila, envia para `/data/local/tmp`, lista as seriais e inicia. No log, o esperado é:

```
snes_arm_daemon: controle "SNES-BT-Controller" -> braço em auto, protocolo william (texto), 1 linha a cada 30 ms
controle encontrado: /dev/input/event2 "SNES-BT-Controller" ...
conectado ao braço em /dev/ttyUSB0 (aguardando o boot do ESP32)
braço pronto em /dev/ttyUSB0: enviando o estado atual 0xFFFF
```

Para acompanhar o log, rode `adb shell tail -f /data/local/tmp/snes_arm_daemon.log`. Para ver também o que o
seu ESP32 imprime (`BTN_A`, ...), rode o deploy com `EXTRA="--verbose --echo"`.

**Se o ESP32 do joystick também estiver na USB da Raspberry**, fixe a porta do braço. Senão, com o braço
desligado, o driver abriria a serial do joystick:

```bash
adb shell /data/local/tmp/snes_arm_daemon --list
```

```bash
DEVICE=usb:1-1.3 sh braco/scripts/deploy.sh
```

Para parar (no modo william, o braço para porque as linhas param):

```bash
sh braco/scripts/deploy.sh stop
```

### 5.4. Se for usar o firmware `SNES_ARM_ESP32` em vez do seu

```bash
PROTOCOLO=frame sh braco/scripts/deploy.sh
```

## 6. Testar sem braço

O teste automático **na Raspberry** usa o driver real do Raphael (uma cópia de teste, com outro nome) e um
"ESP32 falso" que lê as linhas como o seu firmware e mede o intervalo entre elas:

```bash
python3 braco/scripts/device_test.py
```

No Windows, use `python braco/scripts/device_test.py`. Para ver o que o **joystick de verdade** mandaria ao seu
ESP32, com o app e o driver do Raphael rodando:

```bash
adb shell "cd /data/local/tmp && (./frame_sniffer --william --duration-ms 15000 > real.log &); sleep 1; ./snes_arm_daemon --device \$(sed -n 's/^PTY //p' real.log) --boot-wait-ms 0 & sleep 15; kill \$!; cat real.log"
```

Cada linha do resultado é uma "rajada": `linha=256 A -> ... garra=+1 x34 linhas em 998 ms`.

### 6.1. Demonstração sem ESP32: o braço no navegador

O simulador (`braco/simulador/arm_sim.py`) faz o papel do ESP32 **com o seu firmware**. Ele lê no máximo
uma linha a cada 25 ms, anda 1° por botão e respeita os seus limites `MIN_*`/`MAX_*`. O resto da cadeia é o
real: joystick → app → driver do Raphael → `snes_arm_daemon` na Raspberry. Só o ESP32 é trocado pelo PC.

Em um terminal, na raiz do repositório, deixe rodando:

```bash
python braco/simulador/arm_sim.py
```

Em outro terminal, ligue a Raspberry ao simulador (o `adb reverse` precisa ser refeito depois de cada reboot):

```bash
adb reverse tcp:7878 tcp:7878
```

```bash
adb shell "nohup /data/local/tmp/snes_arm_daemon --device tcp:127.0.0.1:7878 --verbose > /data/local/tmp/snes_arm_sim.log 2>&1 &"
```

Abra **http://127.0.0.1:8787** e use o joystick. O painel mostra:
- o braço de lado (ombro e cotovelo) e de cima (base e garra);
- os botões recebidos e o número de cada linha;
- os ângulos de cada servo;
- as linhas/s (cerca de 33 com um botão apertado);
- a "fila", ou seja, as linhas esperando o `loop()`. Ela deve ficar em 0 ou 1.

Para parar (o simulador atende um driver por vez, então pare um antes de subir outro):

```bash
adb shell "pkill -f '[s]nes_arm_daemon --device tcp'; pkill -f '[s]nes_input_daemon --socket demo'"
```

Sem joystick, dá para mandar botões pelo daemon do Raphael com o `ipc_probe`, que vai para a Raspberry junto
com o `device_test.py`. Nesse caso, o `snes_arm_daemon` usa `--name SNES-ARM-DEMO`:

```bash
adb shell "cd /data/local/tmp/armtest && (./snes_input_daemon --socket demo --name SNES-ARM-DEMO --client-timeout-ms 3000 > /dev/null 2>&1 &); sleep 0.5; (./snes_arm_daemon --device tcp:127.0.0.1:7878 --name SNES-ARM-DEMO > /dev/null 2>&1 &); sleep 2; ./ipc_probe --socket demo --hold-ms 1500 FFEF FFFF FFFE FFFF FFBF FFFF FEFF FFFF"
```

Isso faz ↑, B, ← e A, 1,5 s cada.

## 7. Resultados de 06/11 (Raspberry real)

| Teste | Resultado |
|---|---|
| Testes unitários do driver, rodando na Raspberry (inclui o protocolo william) | ✅ 61/61 |
| Teste integrado, protocolo **frame** (driver do Raphael → driver do braço → ESP32 falso) | ✅ 9/9 |
| Teste integrado, protocolo **william**: os 11 estados chegam certos, ~1 linha a cada 30 ms, nunca menos de 25 ms, linhas param quando o controle cai | ✅ 10/10 |
| Serial caiu → driver larga o link sem gastar CPU (antes ia a 110%) | ✅ |
| **Joystick real** (Bluetooth) → app → driver do Raphael → driver do braço → ESP32 falso: B, A, X e ↓ ← chegaram como `97`, `352`, `608`... (461 linhas, 0 inválidas, intervalo mínimo de 28 ms) | ✅ |
| `deploy.sh` no Windows; driver instalado e rodando na Raspberry (0,8% de CPU) | ✅ |
| Simulador no navegador com o seu firmware emulado, alimentado pelo joystick real (seção 6.1) | ✅ |
| **Joystick real → Raspberry → USB → seu ESP32 com o `firmwareBraco.ino`** (seção 7.1) | ✅ B, Y, A, X e ← respondidos pelo ESP |
| Servos se mexendo no braço montado | ⏳ não verificado: não dava para ver o braço daqui |

### 7.1. Teste com o seu ESP32 real

ESP32 DevKit com o seu `firmwareBraco.ino`, ligado numa USB-A da Raspberry:

- A Raspberry reconheceu a placa: `CP2102 USB to UART Bridge Controller` (10c4:ea60), porta física
  **`usb:1-1.4`**, `/dev/ttyUSB0`.
- Driver iniciado com:

```bash
adb shell "nohup /data/local/tmp/snes_arm_daemon --device usb:1-1.4 --verbose --echo > /data/local/tmp/snes_arm_daemon.log 2>&1 &"
```

Trecho do `adb logcat -s SnesBtReceiver SnesDaemon SnesArmDaemon` (A apertado no joystick):

```
12:22:28.767  SnesBtReceiver: estado 0xFEFF [A]         ← app recebeu o A do joystick (Bluetooth)
12:22:28.768  SnesArmDaemon:  estado 0xFEFF (evento)     ← driver do braço leu o gamepad (1 ms depois)
12:22:28.771  SnesArmDaemon:  esp32: BTN_A               ← seu firmware executou pressButton(BTN_A)
12:22:28.821  SnesArmDaemon:  esp32: BTN_A               ← ...e repete enquanto A está apertado
12:22:28.847  SnesArmDaemon:  esp32: BTN_A
```

- Em poucos segundos de teste, o ESP respondeu 148 vezes: `BTN_B` 38, `BTN_Y` 32, `BTN_A` 32, `BTN_X` 46.
- As respostas vêm a cada 25–50 ms, no ritmo do seu `loop()`, ou seja, **nenhuma linha fica acumulada**.
- Do joystick até o ESP leva ~4 ms.
- **Não foi preciso mudar nada no seu firmware nem no driver.**

O que aparece em cada log durante o teste:

| Tag | Quem | O que mostra |
|---|---|---|
| `SnesBtReceiver` | app do Raphael | botões que chegaram do joystick (`grip=` é o acelerômetro, que o braço ignora) |
| `SnesDaemon` | driver do Raphael | daemon subindo e o app conectando |
| `SnesArmDaemon` | driver do braço | `estado 0x...` a cada mudança e, com `--echo`, o que o seu ESP imprime (`esp32: 256`, `esp32: BTN_A`) |

Para ver ao vivo:

```bash
adb logcat -s SnesBtReceiver SnesDaemon SnesArmDaemon
```

Ou só o driver do braço:

```bash
adb shell tail -f /data/local/tmp/snes_arm_daemon.log
```

### 7.2. Depois de reiniciar a Raspberry

**Os drivers continuam instalados**: os binários ficam em `/data/local/tmp`, que sobrevive ao reboot, então não
é preciso compilar nem enviar de novo. O que **não** sobe sozinho são os **processos**.

Em 11/11, o braço parou de mexer depois de um reboot por isso: o lado do Raphael (daemon e app) tinha sido
religado e o joystick conectou, mas o driver do braço não foi iniciado.

**O driver do Raphael também não sobe sozinho.** Só o **app** sobe sozinho (`BOOT_COMPLETED: iniciando
ControllerService`). Logo depois do boot, ele registra `Daemon indisponível @snesbt_input: Connection refused`
até alguém iniciar o `snes_input_daemon` pelo adb. Por isso, o comando que vocês rodavam para o lado do Raphael
(`nohup /data/local/tmp/snes_input_daemon --allow-uid ...`) é obrigatório depois de cada reboot. O
`am start-foreground-service` não é, porque o app já se inicia sozinho.

Para religar tudo, um comando só, que já inclui o do Raphael (PowerShell ou Git Bash):

```bash
adb shell sh /data/local/tmp/rasp_start.sh
```

O `braco/scripts/rasp_start.sh`, que o `deploy.sh` envia para a Raspberry, faz o seguinte:
1. sobe o driver do Raphael, se ele ainda não estiver rodando;
2. inicia o serviço do app;
3. sobe o driver do braço **como root**, via `su 0`, com `--verbose --echo`;
4. mostra o começo do log.

Depois, acompanhe com `adb shell tail -f /data/local/tmp/snes_arm_daemon.log` e aperte um botão: devem
aparecer `estado 0x...` e `esp32: BTN_...`.

Outros usos:

```bash
adb shell sh /data/local/tmp/rasp_start.sh usb:1-1.4
```

```bash
adb shell sh /data/local/tmp/rasp_start.sh stop
```

O primeiro fixa a porta (necessário com os dois ESP32 na USB); o segundo para o driver do braço.

Se a Raspberry ainda não tiver o script, envie uma vez:

```bash
adb push braco/scripts/rasp_start.sh /data/local/tmp/
```

**Cuidados:**
- **Não use `adb root`.** Ele reinicia o adb e derruba tudo o que foi iniciado por ele, inclusive o driver do
  Raphael. O script e o `deploy.sh` usam `su 0`, que dá root só ao driver do braço, sem reiniciar nada.
- **`Permission denied` ao abrir `/dev/ttyUSB0`** quer dizer que o driver do braço foi iniciado sem root.
  `/dev/ttyUSB0` só pode ser aberto pelo root. Rode o `rasp_start.sh`.
- **O joystick precisa estar conectado.** Confira no `adb logcat -s SnesBtReceiver`: o app deve estar em `READY`.
  `READY -> RECOVERING (desconectado (status=8))` é o joystick que caiu, por estar desligado, longe ou ter mudado
  para o modo com fio pelo Select. O app reconecta sozinho, e `status=133` na primeira tentativa é normal.
- Se o ESP for para outra entrada USB, o número muda. Veja qual é com
  `adb shell /data/local/tmp/snes_arm_daemon --list`.
- Um `Address already in use` no `SnesDaemon` só quer dizer que o driver do Raphael já estava rodando.

### 7.3. Subir sozinho no boot (sem o comando da 7.2)

São dois caminhos:

| Caminho | Quando usar |
|---|---|
| **A. Serviço do `init` nesta placa** (abaixo) | Agora, para as demonstrações. Funciona porque a imagem é `userdebug`, com SELinux `Permissive` e bootloader destravado (`orange`) |
| **B. Na imagem do Android** | Versão final: o `braco/aosp/` (`Android.bp` + `snes_arm_daemon.rc`) põe o driver em `/vendor/bin` e o `init` o sobe. Exige recompilar a imagem no computador de build do AOSP, junto com o driver do Raphael |

**Caminho A.** O arquivo `braco/aosp/braco_autostart.rc` declara um serviço que, quando o boot termina
(`sys.boot_completed=1`), roda o `/data/local/tmp/rasp_start.sh` como root. Ele já foi copiado para
`/data/local/tmp`; o `deploy.sh` também o envia. O que falta é colocá-lo na pasta de inicialização do sistema,
`/vendor/etc/init`.

> **Atenção:** esse passo **desliga a verificação de integridade das partições do sistema (dm-verity)** e
> deixa `/vendor` gravável. É normal numa placa de desenvolvimento, mas é uma configuração de segurança.
> Faça só na placa do projeto. Desfazer está logo abaixo.

Uma vez, no PowerShell:

```bash
adb root
```

```bash
adb disable-verity
```

Se ele pedir reboot (`Now reboot your device for settings to take effect`), rode `adb reboot`, espere a placa
voltar e rode `adb root` de novo. Depois:

```bash
adb remount
```

```bash
adb shell cp /data/local/tmp/braco_autostart.rc /vendor/etc/init/braco_autostart.rc
```

```bash
adb shell chmod 644 /vendor/etc/init/braco_autostart.rc
```

```bash
adb reboot
```

Depois do boot, sem rodar nada, confira:

```bash
adb shell "ps -A -o PID,USER,ARGS | grep snes_"
```

Devem aparecer o `snes_input_daemon` e o `snes_arm_daemon`. Também dá para ver no log:
`adb logcat -s SnesArmDaemon`.

- O script continua em `/data/local/tmp/rasp_start.sh`. Se ele mudar, basta enviar o novo (o `deploy.sh` faz
  isso), sem mexer no `/vendor` de novo.
- Com o início automático, o driver do braço usa `--device auto`. Com os dois ESP32 na USB, mude a linha do
  serviço para `... rasp_start.sh usb:<porta>`.
- O driver do Raphael passa a rodar como root, e não como `shell`. Não muda nada para o app.

**Desfazer:**

```bash
adb root
```

```bash
adb remount
```

```bash
adb shell rm /vendor/etc/init/braco_autostart.rc
```

```bash
adb enable-verity
```

```bash
adb reboot
```

## 8. Pendências e avisos

1. **O joystick manda ↓ (e às vezes ↓ ←) parado na mesa.** Em 06/11, o app registrava `0xFFDF [DOWN]` e
   `0xFF9F [DOWN LEFT]` sem ninguém mexer. No braço, isso faz **o ombro descer e a base girar sozinhos**.
   Provavelmente é o centro do analógico ou do acelerômetro (firmware do Raphael, commit "ajuste acelerometro").
   Calibre antes de ligar os servos, ou teste com o braço solto.
2. **Start não para o braço no seu firmware** (cai no `default`). Na sua wiki, Start seria "parar os movimentos".
   Como nada anda sem um botão de movimento, hoje não faz falta. Se quiser parar mesmo com outro botão apertado,
   trate o bit 3 no `lerComando()`.
3. Seu firmware imprime duas linhas por linha recebida: o número (`100`, em hexadecimal) e o `BTN_x`. Isso não
   atrapalha, mas deixa o `--echo` barulhento.
4. A primeira linha do `.ino` tem um comentário colado: `//sudo chmod a+rw /dev/ttyUSB0sudo chmod ...`.
   É inofensivo.
5. Os limites `MIN_*`/`MAX_*` precisam ser **calibrados no braço montado**, para o servo não forçar o acrílico.
6. O `snes_input_daemon` instalado na placa é de 21/10. Na branch `driver` há uma versão de 05/11, com watchdog
   de 500 ms, que o Raphael ainda instala com o `scripts/deploy.sh` da raiz. O driver do braço funciona com as duas.

### 8.1. Possíveis mudanças com o braço montado

Nenhuma é obrigatória para funcionar. Elas aparecem conforme o comportamento do braço real:

| Se acontecer | Onde mudar | O quê |
|---|---|---|
| Um servo anda no sentido contrário | seu `.ino`, `pressButton()` | trocar o `1`/`-1` daquele botão |
| Servo força o acrílico no fim do curso | seu `.ino` | ajustar `MIN_*`/`MAX_*` da junta (calibração) |
| Braço rápido ou lento demais | driver, sem mexer no firmware | `--keepalive-ms N` (mínimo 26; 30 = ~33°/s, 50 = 20°/s) |
| Quer que Start pare tudo | seu `.ino`, `lerComando()` | se o bit 3 estiver em 1, não chamar `pressButton()` |
| Ombro desce ou base gira sozinhos | joystick (Raphael) | calibrar o centro do analógico ou do acelerômetro (item 1 acima) |
| Servos tremem ou o ESP reinicia ao mexer | alimentação | fonte de 5 V externa (3–5 A) com GND comum. A USB não aguenta 4 SG90 |
| ESP em outra entrada USB ou ESP do joystick também na USB | comando do driver | `--device usb:<porta>` (veja com `--list`) |
| Placa trocada por uma com CH340 | hardware | não funciona: o kernel da Raspberry só tem os drivers do CP2102, do PL2303 e da USB nativa (`ttyACM`) |
| Quer subir o driver sozinho no boot | imagem do Android | `braco/aosp/` tem o `.rc` (precisa recompilar a imagem) |

## 9. Onde está cada coisa

| Arquivo | O quê |
|---|---|
| `braco/daemon/src/` | Driver do braço (`forwarder.cpp` monta as linhas; `serial_link.cpp` cuida da USB) |
| `braco/daemon/tools/frame_sniffer.cpp` | ESP32 falso (`--william` lê o seu formato) |
| `braco/daemon/tests/daemon_tests.cpp` | Testes unitários |
| `braco/scripts/deploy.sh`, `braco/scripts/device_test.py` | Instalar na Raspberry e teste automático |
| `braco/docs/OPERACAO.md` | Todas as opções do driver |
| `braco/firmware_esp32/SNES_ARM_ESP32/` | O firmware alternativo (`--protocolo frame`) |
