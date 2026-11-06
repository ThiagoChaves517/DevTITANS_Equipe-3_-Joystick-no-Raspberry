#!/usr/bin/env sh
# Compila o snes_arm_daemon e as ferramentas para o Raspberry (GloDroid arm64) com o NDK.
# Uso: ./build.sh [pasta_saida]   (NDK em $ANDROID_NDK_HOME ou no SDK local)
#
# Saída: snes_arm_daemon, frame_sniffer (ESP32 falso) e snes_fake_pad (controle falso).
set -eu
cd "$(dirname "$0")"
OUT="${1:-build}"
API="${API:-34}"
FW=../firmware_esp32/SNES_ARM_ESP32

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
    # Mais nova que tenha o toolchain (uma instalação incompleta não tem toolchains/llvm).
    for ndk in $(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V -r); do
        if [ -d "$ndk/toolchains/llvm/prebuilt" ]; then ANDROID_NDK_HOME="$ndk"; break; fi
    done
fi
[ -d "${ANDROID_NDK_HOME:-}/toolchains/llvm/prebuilt" ] || { echo "NDK não encontrado; defina ANDROID_NDK_HOME" >&2; exit 1; }
HOST="$(ls "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/" | head -1)"
CXX="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android$API-clang++"
FLAGS="-std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ -I$FW"

mkdir -p "$OUT"
"$CXX" $FLAGS src/main.cpp src/snes_register.cpp src/forwarder.cpp src/serial_link.cpp src/evdev_reader.cpp \
    "$FW/FrameProtocol.cpp" -llog -o "$OUT/snes_arm_daemon"
"$CXX" $FLAGS tools/frame_sniffer.cpp "$FW/FrameProtocol.cpp" -o "$OUT/frame_sniffer"
"$CXX" $FLAGS tools/snes_fake_pad.cpp -o "$OUT/snes_fake_pad"
echo "Compilado em $OUT/: snes_arm_daemon frame_sniffer snes_fake_pad ($(basename "$ANDROID_NDK_HOME"), android$API arm64)"
