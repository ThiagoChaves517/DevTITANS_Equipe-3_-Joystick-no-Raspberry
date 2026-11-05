#!/usr/bin/env sh
# Compila e roda os testes do daemon no host (não precisa de Linux nem de hardware).
set -eu
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/snes_daemon_tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 -g \
    tests/daemon_tests.cpp src/ipc_protocol.cpp src/controller_device.cpp src/client_session.cpp -o "$OUT"
"$OUT"
