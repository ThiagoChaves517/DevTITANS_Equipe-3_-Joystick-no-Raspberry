#!/usr/bin/env bash
# Compila a bridge e as ferramentas.
#
#   ./build.sh            binários ARM64 para a Raspberry (Android NDK)  -> out/android/
#   ./build.sh host       bridge para o computador (entrada stdin, backends serial/tcp/none) -> out/host/
#   ./build.sh test       compila e roda os testes de lógica no computador
#   ./build.sh deploy     compila para Android e envia para /data/local/tmp (adb root)
#
# NDK: usa $NDK se definido; senão procura em ~/Library/Android/sdk/ndk/ (mesma
# versão da wiki do driver: 27.0.12077973).
set -euo pipefail

cd "$(dirname "$0")"

SOURCES=(main.c mapping.c arm_model.c input.c input_stdin.c input_evdev.c
         backend.c link_text.c backend_serial.c backend_tcp.c backend_gpio.c)
CFLAGS=(-O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE)

find_ndk() {
    if [[ -n "${NDK:-}" ]]; then echo "$NDK"; return; fi
    for candidate in "$HOME/Library/Android/sdk/ndk/27.0.12077973" "$HOME/Android/Sdk/ndk/27.0.12077973" \
                     "$HOME"/Library/Android/sdk/ndk/* "$HOME"/Android/Sdk/ndk/*; do
        if [[ -d "$candidate/toolchains/llvm/prebuilt" ]]; then echo "$candidate"; return; fi
    done
    echo "NDK não encontrado. Defina NDK=/caminho/do/ndk" >&2
    exit 1
}

build_android() {
    local ndk host_tag cc
    ndk="$(find_ndk)"
    host_tag="$(ls "$ndk/toolchains/llvm/prebuilt" | head -1)"
    cc="$ndk/toolchains/llvm/prebuilt/$host_tag/bin/aarch64-linux-android35-clang"

    mkdir -p out/android
    "$cc" "${CFLAGS[@]}" "${SOURCES[@]}" -lm -o out/android/snes_arm_bridge
    "$cc" "${CFLAGS[@]}" tools/snes_fake_pad.c -o out/android/snes_fake_pad
    echo "ok: out/android/snes_arm_bridge out/android/snes_fake_pad ($(basename "$ndk"))"
}

build_host() {
    mkdir -p out/host
    cc "${CFLAGS[@]}" "${SOURCES[@]}" -lm -o out/host/snes_arm_bridge
    echo "ok: out/host/snes_arm_bridge"
}

run_tests() {
    mkdir -p out/host
    cc "${CFLAGS[@]}" tests/test_logic.c mapping.c arm_model.c -lm -o out/host/test_logic
    ./out/host/test_logic
}

case "${1:-android}" in
    android) build_android ;;
    host)    build_host ;;
    test)    run_tests ;;
    deploy)
        build_android
        adb root >/dev/null
        adb wait-for-device
        adb push out/android/snes_arm_bridge out/android/snes_fake_pad /data/local/tmp/
        adb shell chmod 755 /data/local/tmp/snes_arm_bridge /data/local/tmp/snes_fake_pad
        ;;
    *) echo "uso: $0 [android|host|test|deploy]"; exit 2 ;;
esac
