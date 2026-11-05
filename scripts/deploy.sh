#!/usr/bin/env sh
# Implanta app + daemon no Raspberry via ADB (modo desenvolvimento; ver docs/OPERACAO.md).
#   scripts/deploy.sh            compila, instala e inicia tudo
#   scripts/deploy.sh stop       para serviço e daemon
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG=com.snesbt.receiver
SVC="$PKG/.service.ControllerService"
REMOTE=/data/local/tmp/snes_input_daemon
LOG=/data/local/tmp/snes_input_daemon.log
WATCHDOG_MS="${WATCHDOG_MS:-}"

stop_all() {
    adb shell am stopservice -n "$SVC" >/dev/null 2>&1 || true
    adb shell "pkill -f $REMOTE" >/dev/null 2>&1 || true
}

if [ "${1:-}" = "stop" ]; then stop_all; echo "Parado."; exit 0; fi

if [ -z "${JAVA_HOME:-}" ] && [ -d "/Applications/Android Studio.app/Contents/jbr/Contents/Home" ]; then
    export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
fi

echo "== build"
(cd "$ROOT" && ./gradlew -q :app:assembleDebug)
"$ROOT/daemon/build.sh" >/dev/null

echo "== instalar"
stop_all
adb shell am force-stop "$PKG" || true
adb install -r -g "$ROOT/app/build/outputs/apk/debug/app-debug.apk" >/dev/null
adb shell pm grant "$PKG" android.permission.BLUETOOTH_SCAN
adb shell pm grant "$PKG" android.permission.BLUETOOTH_CONNECT
adb push "$ROOT/daemon/build/snes_input_daemon" "$REMOTE" >/dev/null
adb shell chmod 755 "$REMOTE"

APP_UID="$(adb shell pm list packages -U "$PKG" | sed -n "s/^package:$PKG uid:\([0-9]*\).*/\1/p" | tr -d '\r')"
[ -n "$APP_UID" ] || { echo "uid do app não encontrado" >&2; exit 1; }

echo "== iniciar daemon (uid permitido: $APP_UID)"
adb shell "nohup $REMOTE --allow-uid $APP_UID > $LOG 2>&1 &"
sleep 1
adb shell "pidof snes_input_daemon" >/dev/null || { adb shell cat "$LOG"; echo "daemon não iniciou" >&2; exit 1; }

echo "== iniciar serviço"
if [ -n "$WATCHDOG_MS" ]; then
    adb shell am start-foreground-service -n "$SVC" --el watchdog_ms "$WATCHDOG_MS" >/dev/null
else
    adb shell am start-foreground-service -n "$SVC" >/dev/null
fi
sleep 3
adb shell dumpsys activity service "$SVC" | sed -n '/^state=/,$p' | head -8
