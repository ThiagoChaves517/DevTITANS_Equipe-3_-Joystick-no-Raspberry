#!/usr/bin/env sh
# Implanta o snes_arm_daemon no Raspberry via ADB (modo desenvolvimento; ver braco/docs/OPERACAO.md).
#   braco/scripts/deploy.sh                 compila, envia e inicia (--device auto)
#   DEVICE=usb:1-1.3 braco/scripts/deploy.sh escolhe a serial do braço
#   braco/scripts/deploy.sh stop            para o daemon
#
# O snes_input_daemon + app (branch driver) sobem com o scripts/deploy.sh da raiz.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REMOTE=/data/local/tmp
LOG=$REMOTE/snes_arm_daemon.log
DEVICE="${DEVICE:-auto}"

stop_all() { adb shell "pkill -f $REMOTE/snes_arm_daemon" >/dev/null 2>&1 || true; }

if [ "${1:-}" = "stop" ]; then stop_all; echo "Parado."; exit 0; fi

echo "== build"
"$ROOT/daemon/build.sh" >/dev/null

echo "== enviar"
adb root >/dev/null 2>&1 || true  # /dev/ttyUSB* é root:root 0600 na imagem atual
adb wait-for-device
stop_all
adb push "$ROOT/daemon/build/snes_arm_daemon" "$ROOT/daemon/build/frame_sniffer" "$ROOT/daemon/build/snes_fake_pad" \
    "$REMOTE/" >/dev/null
adb shell chmod 755 "$REMOTE/snes_arm_daemon" "$REMOTE/frame_sniffer" "$REMOTE/snes_fake_pad"

echo "== seriais vistas pela placa"
adb shell "$REMOTE/snes_arm_daemon --list"

echo "== iniciar (--device $DEVICE)"
adb shell "nohup $REMOTE/snes_arm_daemon --device $DEVICE > $LOG 2>&1 &"
sleep 1
adb shell "pidof snes_arm_daemon" >/dev/null || { adb shell cat "$LOG"; echo "daemon não iniciou" >&2; exit 1; }
adb shell cat "$LOG"
echo "Log: adb shell tail -f $LOG"
