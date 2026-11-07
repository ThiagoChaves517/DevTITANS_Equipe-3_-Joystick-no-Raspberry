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
    # SDK do Android Studio: Mac (~/Library/Android/sdk), Linux (~/Android/Sdk), Windows/Git Bash (%LOCALAPPDATA%\Android\Sdk).
    for SDK in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk" \
               "${LOCALAPPDATA:-}/Android/Sdk"; do
        [ -n "$SDK" ] && [ -d "$SDK/ndk" ] || continue
        # Mais nova que tenha o toolchain (uma instalação incompleta não tem toolchains/llvm).
        for ndk in $(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V -r); do
            if [ -d "$ndk/toolchains/llvm/prebuilt" ]; then ANDROID_NDK_HOME="$ndk"; break; fi
        done
        [ -n "${ANDROID_NDK_HOME:-}" ] && break
    done
fi
[ -d "${ANDROID_NDK_HOME:-}/toolchains/llvm/prebuilt" ] || { echo "NDK não encontrado; defina ANDROID_NDK_HOME" >&2; exit 1; }
HOST="$(ls "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/" | head -1)"
# clang++ com --target em vez do atalho aarch64-linux-androidNN-clang++: no Windows o atalho é um .cmd,
# que o Git Bash não executa; o clang++ (clang++.exe) existe igual nos três sistemas.
CXX="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/clang++"
FLAGS="--target=aarch64-linux-android$API -std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ -I$FW"

mkdir -p "$OUT"
"$CXX" $FLAGS src/main.cpp src/snes_register.cpp src/forwarder.cpp src/serial_link.cpp src/evdev_reader.cpp \
    "$FW/FrameProtocol.cpp" -llog -o "$OUT/snes_arm_daemon"
"$CXX" $FLAGS tools/frame_sniffer.cpp "$FW/FrameProtocol.cpp" -o "$OUT/frame_sniffer"
"$CXX" $FLAGS tools/snes_fake_pad.cpp -o "$OUT/snes_fake_pad"
echo "Compilado em $OUT/: snes_arm_daemon frame_sniffer snes_fake_pad ($(basename "$ANDROID_NDK_HOME"), android$API arm64)"
