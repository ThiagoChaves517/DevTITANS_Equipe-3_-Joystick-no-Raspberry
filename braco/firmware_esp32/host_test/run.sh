#!/usr/bin/env bash
# Compila o firmware SNES_ARM_ESP32 para o computador (Arduino.h simulado) e roda.
#   ./run.sh                 interativo
#   ./run.sh < roteiro.txt   roteiro (linhas "@ms" = esperar)
set -euo pipefail
cd "$(dirname "$0")"

SKETCH=../SNES_ARM_ESP32
mkdir -p out
# O .ino é C++ comum; o Arduino só adiciona protótipos (aqui as funções já vêm antes do uso).
cp "$SKETCH/SNES_ARM_ESP32.ino" out/sketch.cpp

c++ -std=c++17 -O1 -Wall -Wextra -I. -I"$SKETCH" \
    out/sketch.cpp "$SKETCH/ServoJoint.cpp" host_main.cpp -o out/firmware_host

exec ./out/firmware_host
