#!/usr/bin/env python3
"""
Simulador do braço (4x SG90): faz o papel do ESP32 com o firmware SNES_ARM_ESP32.

Recebe os quadros de 8 bytes que o snes_arm_daemon repassa (53 4E 01 01 seq p0 p1 CRC),
aplica a MESMA tabela da wiki do William (ArmMapping.h) e o mesmo ritmo do firmware
(1 grau a cada 25 ms, limites do fluxograma, direções zeradas após 250 ms sem quadro).
Como o braço real, não responde nada.

Uso (só biblioteca padrão do Python 3):
    python3 arm_sim.py                       # escuta em 127.0.0.1:7878, painel em http://127.0.0.1:8787
    adb reverse tcp:7878 tcp:7878
    adb shell /data/local/tmp/snes_arm_daemon --device tcp:127.0.0.1:7878 --protocolo frame
"""

import argparse
import json
import os
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Mesmos números de braco/firmware_esp32/SNES_ARM_ESP32/ArmConfig.h
JOINTS = [
    {"name": "base",     "min": 0,  "max": 180, "initial": 90},
    {"name": "ombro",    "min": 20, "max": 160, "initial": 90},
    {"name": "cotovelo", "min": 20, "max": 160, "initial": 90},
    {"name": "garra",    "min": 20, "max": 100, "initial": 60},
]
INTERVALO_S = 0.025
TIMEOUT_SERIAL_S = 0.250

# Bits do registrador SNES (ativo em LOW) — ArmMapping.h
BITS = {"B": 0, "Y": 1, "SELECT": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7,
        "A": 8, "X": 9, "L": 10, "R": 11}
SYNC1, SYNC2, VERSION, CONTROLLER_STATE = 0x53, 0x4E, 0x01, 0x01


def crc8(data):
    crc = 0
    for x in data:
        crc ^= x
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def pressed(state, name):
    return (state >> BITS[name]) & 1 == 0


def axis(plus, minus):
    return 0 if plus == minus else (1 if plus else -1)


def directions(state):
    """ArmMapping::fromState: tabela da wiki 'Braço Robótico ‐ Semana 09\\09‐21\\10'."""
    if pressed(state, "START"):
        return [0, 0, 0, 0]
    return [axis(pressed(state, "RIGHT"), pressed(state, "LEFT")),
            axis(pressed(state, "UP"), pressed(state, "DOWN")),
            axis(pressed(state, "B"), pressed(state, "Y")),
            axis(pressed(state, "A"), pressed(state, "X"))]


class FrameDecoder:
    """Mesma lógica do Protocol::FrameDecoder do firmware: acha o SYNC e confere versão, tipo e CRC."""

    def __init__(self):
        self.buf = bytearray()
        self.frames = self.errors = self.discarded = 0

    def push(self, data):
        out = []
        for b in data:
            self.buf.append(b)
            while True:
                if self.buf and self.buf[0] != SYNC1 or len(self.buf) >= 2 and self.buf[1] != SYNC2:
                    del self.buf[0]
                    self.discarded += 1
                    continue
                if len(self.buf) < 8:
                    break
                f = self.buf[:8]
                if f[2] != VERSION or not 0x01 <= f[3] <= 0x0A or crc8(f[:7]) != f[7]:
                    self.errors += 1
                    del self.buf[0]
                    continue
                out.append((f[3], f[4], f[5] | (f[6] << 8)))
                del self.buf[:8]
                self.frames += 1
                break
        return out


class ArmEmulator:
    def __init__(self):
        self.lock = threading.Lock()
        self.angles = [j["initial"] for j in JOINTS]
        self.state = 0xFFFF
        self.dirs = [0, 0, 0, 0]
        self.last_rx = 0.0
        self.connected = False
        self.client = ""
        self.decoder = FrameDecoder()
        self.frame_times = []
        self.last_seq = None
        self.watchdog_events = 0
        self.log = []

    def add_log(self, text):
        self.log.append(time.strftime("%H:%M:%S ") + text)
        del self.log[:-40]
        print(f"[SIM] {text}", flush=True)

    def apply_state(self, state):
        state |= 0xF000
        if state == self.state:
            return
        self.state = state
        self.dirs = directions(state)
        names = " ".join(n for n in BITS if pressed(state, n)) or "(nada)"
        self.add_log(f"estado 0x{state:04X} {names} -> base={self.dirs[0]:+d} ombro={self.dirs[1]:+d} "
                     f"cotovelo={self.dirs[2]:+d} garra={self.dirs[3]:+d}")

    def feed(self, data):
        with self.lock:
            now = time.monotonic()
            for ftype, seq, state in self.decoder.push(data):
                if ftype != CONTROLLER_STATE:
                    continue
                self.last_rx = now
                self.last_seq = seq
                self.frame_times = [t for t in self.frame_times if now - t < 1.0] + [now]
                self.apply_state(state)

    def step(self):
        with self.lock:
            if self.state != 0xFFFF and time.monotonic() - self.last_rx > TIMEOUT_SERIAL_S:
                self.watchdog_events += 1
                self.add_log("250 ms sem quadro: direções zeradas")
                self.apply_state(0xFFFF)
            for i, j in enumerate(JOINTS):
                self.angles[i] = max(j["min"], min(j["max"], self.angles[i] + self.dirs[i]))

    def snapshot(self):
        with self.lock:
            now = time.monotonic()
            return {
                "angles": self.angles, "joints": JOINTS, "state": self.state, "dirs": self.dirs,
                "pressed": [n for n in BITS if pressed(self.state, n)],
                "connected": self.connected, "client": self.client,
                "rate_hz": len([t for t in self.frame_times if now - t < 1.0]),
                "frames": self.decoder.frames, "errors": self.decoder.errors, "discarded": self.decoder.discarded,
                "last_seq": self.last_seq, "watchdog_events": self.watchdog_events, "log": self.log[-14:],
            }


def tcp_server(arm, host, port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(1)
    print(f"[SIM] braço escutando em tcp://{host}:{port}", flush=True)
    while True:
        conn, addr = srv.accept()
        with arm.lock:
            arm.connected, arm.client = True, f"{addr[0]}:{addr[1]}"
        arm.add_log(f"snes_arm_daemon conectado ({addr[0]}:{addr[1]})")
        with conn:
            while True:
                try:
                    data = conn.recv(512)
                except OSError:
                    data = b""
                if not data:
                    break
                arm.feed(data)  # nunca responde: o braço é só ouvinte
        with arm.lock:
            arm.connected = False
        arm.add_log("snes_arm_daemon desconectado")


def ticker(arm):
    nxt = time.monotonic()
    while True:
        nxt += INTERVALO_S
        time.sleep(max(0.0, nxt - time.monotonic()))
        arm.step()


def make_http_handler(arm, page_path):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/state"):
                body, ctype = json.dumps(arm.snapshot()).encode(), "application/json"
            elif self.path in ("/", "/index.html"):
                with open(page_path, "rb") as f:  # relido a cada acesso: editar o HTML não exige reiniciar
                    body = f.read()
                ctype = "text/html; charset=utf-8"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    return Handler


def main():
    parser = argparse.ArgumentParser(description="Simulador do braço (firmware SNES_ARM_ESP32)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7878, help="porta que o snes_arm_daemon usa (adb reverse)")
    parser.add_argument("--http-port", type=int, default=8787, help="porta do painel web")
    args = parser.parse_args()

    page_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "painel.html")
    arm = ArmEmulator()
    threading.Thread(target=ticker, args=(arm,), daemon=True).start()
    threading.Thread(target=tcp_server, args=(arm, args.host, args.port), daemon=True).start()

    http = ThreadingHTTPServer((args.host, args.http_port), make_http_handler(arm, page_path))
    print(f"[SIM] painel em http://{args.host}:{args.http_port}", flush=True)
    try:
        http.serve_forever()
    except KeyboardInterrupt:
        print("\n[SIM] encerrado", flush=True)


if __name__ == "__main__":
    main()
