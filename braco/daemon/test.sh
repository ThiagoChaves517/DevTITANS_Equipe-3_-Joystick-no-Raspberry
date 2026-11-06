#!/usr/bin/env sh
# Compila e roda os testes do snes_arm_daemon no host (não precisa de Linux nem de hardware).
# Usa o controller_device.cpp do snes_input_daemon (daemon/ na raiz) para o teste de contrato.
set -eu
cd "$(dirname "$0")"
FW=../firmware_esp32/SNES_ARM_ESP32
OUT="${TMPDIR:-/tmp}/snes_arm_daemon_tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 -g -I"$FW" \
    tests/daemon_tests.cpp src/snes_register.cpp src/forwarder.cpp "$FW/FrameProtocol.cpp" \
    ../../daemon/src/controller_device.cpp -o "$OUT"
"$OUT"
