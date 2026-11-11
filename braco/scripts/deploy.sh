#!/usr/bin/env sh
# Implanta o snes_arm_daemon no Raspberry via ADB (modo desenvolvimento; ver braco/docs/OPERACAO.md).
#   braco/scripts/deploy.sh                    compila, envia e inicia (--device auto, --protocolo william)
#   DEVICE=usb:1-1.3 braco/scripts/deploy.sh    escolhe a serial do braço
#   PROTOCOLO=frame braco/scripts/deploy.sh     firmware SNES_ARM_ESP32 (quadros de 8 bytes) em vez do do William
#   EXTRA="--echo --verbose" braco/scripts/...  mostra no log o que o ESP32 escreve e cada estado do controle
#   braco/scripts/deploy.sh stop               para o daemon
#
# Funciona no Mac/Linux e no Windows (Git Bash), com o NDK do Android Studio.
#
# O snes_input_daemon + app (branch driver) sobem com o scripts/deploy.sh da raiz.
# Depois de um reboot, sem compilar: adb shell sh /data/local/tmp/rasp_start.sh (enviado por este script).
#
# Roda o daemon como root com "su 0" (userdebug), e não com "adb root": o adb root reinicia o adbd e
# derruba o snes_input_daemon do Raphael quando ele foi iniciado pelo adb.
set -eu
# Git Bash (Windows): não converter /data/local/tmp/... em C:/Program Files/Git/data/... nos argumentos do adb.
export MSYS_NO_PATHCONV=1
# ...e por isso os caminhos locais vão já no formato do Windows (C:/...), que o adb.exe entende (pwd -W).
ROOT="$(cd "$(dirname "$0")/.." && { pwd -W 2>/dev/null || pwd; })"
REMOTE=/data/local/tmp
LOG=$REMOTE/snes_arm_daemon.log
DEVICE="${DEVICE:-auto}"
PROTOCOLO="${PROTOCOLO:-william}"
EXTRA="${EXTRA:-}"

adb wait-for-device
# Root sem reiniciar o adbd: "su 0" quando existe; senão, adb root (imagem sem su).
if adb shell "su 0 id" 2>/dev/null | grep -q "uid=0"; then
    AS_ROOT="su 0"
else
    adb root >/dev/null 2>&1 || true
    adb wait-for-device
    AS_ROOT=""
fi

# "[/]data/...": o pkill -f não casa com o próprio "sh -c" do adb.
stop_all() { adb shell "$AS_ROOT pkill -f '[/]data/local/tmp/snes_arm_daemon'" >/dev/null 2>&1 || true; }

if [ "${1:-}" = "stop" ]; then stop_all; echo "Parado."; exit 0; fi

echo "== build"
sh "$ROOT/daemon/build.sh" >/dev/null

echo "== enviar"
stop_all
# Binários enviados antes com adb root são do root: apaga para o push (como shell) poder recriá-los.
adb shell "$AS_ROOT rm -f $REMOTE/snes_arm_daemon $REMOTE/frame_sniffer $REMOTE/snes_fake_pad $REMOTE/rasp_start.sh"
adb push "$ROOT/daemon/build/snes_arm_daemon" "$ROOT/daemon/build/frame_sniffer" "$ROOT/daemon/build/snes_fake_pad" \
    "$ROOT/scripts/rasp_start.sh" "$REMOTE/" >/dev/null
adb shell chmod 755 "$REMOTE/snes_arm_daemon" "$REMOTE/frame_sniffer" "$REMOTE/snes_fake_pad"

echo "== seriais vistas pela placa"
adb shell "$REMOTE/snes_arm_daemon --list"

echo "== iniciar (--device $DEVICE --protocolo $PROTOCOLO $EXTRA)"
adb shell "$AS_ROOT sh -c 'nohup $REMOTE/snes_arm_daemon --device $DEVICE --protocolo $PROTOCOLO $EXTRA > $LOG 2>&1 &'"
sleep 2
adb shell "pidof snes_arm_daemon" >/dev/null || { adb shell cat "$LOG"; echo "daemon não iniciou" >&2; exit 1; }
adb shell cat "$LOG"
echo "Log: adb shell tail -f $LOG"
