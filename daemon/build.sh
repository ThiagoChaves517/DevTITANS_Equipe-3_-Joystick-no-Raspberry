#!/usr/bin/env sh
# Compila o daemon para o Raspberry (GloDroid arm64) com o NDK.
# Uso: ./build.sh [saida]   (NDK detectado em $ANDROID_NDK_HOME ou no SDK local)
set -eu
cd "$(dirname "$0")"
OUT="${1:-build/snes_input_daemon}"
API="${API:-34}"

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
    ANDROID_NDK_HOME="$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)"
fi
[ -d "$ANDROID_NDK_HOME" ] || { echo "NDK não encontrado; defina ANDROID_NDK_HOME" >&2; exit 1; }
HOST="$(ls "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/" | head -1)"
CXX="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android$API-clang++"

mkdir -p "$(dirname "$OUT")"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ \
    src/main.cpp src/ipc_protocol.cpp src/controller_device.cpp src/client_session.cpp src/uinput_device.cpp \
    -llog -o "$OUT"
echo "Daemon compilado: $OUT ($(basename "$ANDROID_NDK_HOME"), android$API arm64)"
