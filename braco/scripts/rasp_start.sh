#!/system/bin/sh
# Roda NA Raspberry: sobe a cadeia do braço depois de um reboot, sem compilar nem enviar nada.
#   1. driver do Raphael (snes_input_daemon), se ainda não estiver rodando;
#   2. serviço do app (com.snesbt.receiver), que conecta o joystick por Bluetooth;
#   3. driver do braço (snes_arm_daemon) como root, via "su 0" (/dev/ttyUSB* é root:root 0600).
#
# Não usa "adb root": ele reinicia o adb e derruba o driver do Raphael quando este foi iniciado pelo adb.
#
# Uso, do PC (o push é uma vez só; o arquivo fica na Raspberry):
#   adb push braco/scripts/rasp_start.sh /data/local/tmp/
#   adb shell sh /data/local/tmp/rasp_start.sh              # --device auto (uma serial USB só)
#   adb shell sh /data/local/tmp/rasp_start.sh usb:1-1.4    # fixa a porta do ESP32 do braço
#   adb shell sh /data/local/tmp/rasp_start.sh stop         # para o driver do braço
T=/data/local/tmp
PKG=com.snesbt.receiver
DEVICE="${1:-auto}"

if [ "$DEVICE" = "stop" ]; then
    su 0 pkill -f "[/]data/local/tmp/snes_arm_daemon"
    echo "driver do braço parado"
    exit 0
fi

UID_APP=$(pm list packages -U $PKG | sed -n 's/.*uid:\([0-9]*\).*/\1/p' | tr -d '\r')
[ -n "$UID_APP" ] || { echo "app $PKG não instalado"; exit 1; }

if pgrep -f "[s]nes_input_daemon --allow-uid" >/dev/null; then
    echo "== driver do Raphael: já rodando"
else
    echo "== driver do Raphael: iniciando (uid do app $UID_APP)"
    nohup $T/snes_input_daemon --allow-uid "$UID_APP" > $T/snes_input_daemon.log 2>&1 &
    sleep 1
fi

echo "== serviço do app"
am start-foreground-service -n $PKG/.service.ControllerService >/dev/null 2>&1

echo "== driver do braço (--device $DEVICE)"
su 0 pkill -f "[/]data/local/tmp/snes_arm_daemon"
sleep 0.3
su 0 sh -c "nohup $T/snes_arm_daemon --device $DEVICE --verbose --echo > $T/snes_arm_daemon.log 2>&1 &"
sleep 3

echo "== processos"
ps -A -o PID,USER,ARGS | grep -E "[s]nes_(input|arm)_daemon"
echo "== log do braço ($T/snes_arm_daemon.log)"
head -6 $T/snes_arm_daemon.log
echo
echo "Ao vivo: adb shell tail -f $T/snes_arm_daemon.log"
