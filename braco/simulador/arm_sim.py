#!/usr/bin/env python3
"""
Simulador do braço MeArm (4 DOF, 4x SG90) — fala o MESMO protocolo do
firmware SNES_ARM_ESP32, então a bridge não sabe que não é o braço real.

Uso (só biblioteca padrão do Python 3):

    python3 arm_sim.py                 # TCP em 127.0.0.1:7878 + painel em http://127.0.0.1:8787
    python3 arm_sim.py --pty           # também cria uma serial falsa (/dev/ttysNNN) para a bridge do Mac

Com a Raspberry:
    adb reverse tcp:7878 tcp:7878
    adb shell /data/local/tmp/snes_arm_bridge --backend tcp

O comportamento imita o firmware: limites por junta, rampa de 180°/s,
home a 60°/s, watchdog de 300 ms sem J e telemetria T a 10 Hz.
A cinemática é aproximada (MeArm: braço e antebraço de ~80 mm, antebraço
com ângulo absoluto por causa do paralelogramo, garra sempre nivelada).
"""

import argparse
import json
import os
import select
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

VERSION = "SNES-ARM-SIM 1.0"

# Mesmos números de braco/firmware_esp32/SNES_ARM_ESP32/ArmConfig.h
JOINTS = [
    {"name": "base",     "min": 10.0, "max": 170.0, "home": 90.0},
    {"name": "ombro",    "min": 45.0, "max": 135.0, "home": 90.0},
    {"name": "cotovelo", "min": 45.0, "max": 135.0, "home": 90.0},
    {"name": "garra",    "min": 30.0, "max": 90.0,  "home": 60.0},
]
WATCHDOG_S = 0.300
TELEMETRY_S = 0.100
UPDATE_S = 0.005
MAX_SPEED = 180.0
HOME_SPEED = 60.0


def clamp(value, lo, hi):
    return max(lo, min(hi, value))


class ArmEmulator:
    """Mesma máquina de estados do SNES_ARM_ESP32.ino."""

    def __init__(self):
        self.lock = threading.Lock()
        self.current = [j["home"] for j in JOINTS]
        self.target = list(self.current)
        self.streaming = False
        self.homing = False
        self.last_frame = 0.0
        self.connected = False
        self.client = ""
        self.note = ""
        self.page = 1
        self.frames = 0
        self.frame_times = []
        self.watchdog_events = 0
        self.log = []

    def add_log(self, text):
        stamp = time.strftime("%H:%M:%S")
        self.log.append(f"{stamp} {text}")
        del self.log[:-40]
        print(f"[SIM] {text}", flush=True)

    def hold_all(self):
        self.target = list(self.current)

    def handle_line(self, line):
        """Recebe uma linha da bridge e devolve a lista de respostas."""
        line = line.strip()
        if not line:
            return []

        with self.lock:
            now = time.monotonic()
            cmd = line[0]

            if cmd == "#":
                self.note = line[1:].strip()
                if self.note.startswith("PAGINA"):
                    try:
                        self.page = int(self.note.split()[1])
                    except (IndexError, ValueError):
                        pass
                self.add_log(f"nota da bridge: {self.note}")
                return []

            if cmd == "J":
                try:
                    values = [float(v) for v in line[1:].split()]
                except ValueError:
                    values = []
                if len(values) != 4:
                    return ["ERR formato: J base ombro cotovelo garra"]
                for i, deg in enumerate(values):
                    self.target[i] = clamp(deg, JOINTS[i]["min"], JOINTS[i]["max"])
                if not self.streaming:
                    self.add_log("recebendo quadros J (watchdog armado)")
                self.last_frame = now
                self.streaming = True
                self.homing = False
                self.frames += 1
                self.frame_times.append(now)
                self.frame_times = [t for t in self.frame_times if now - t < 1.0]
                return []

            if cmd == "G":
                try:
                    pct = float(line[1:])
                except ValueError:
                    return ["ERR formato: G 0-100"]
                if not 0 <= pct <= 100:
                    return ["ERR formato: G 0-100"]
                g = JOINTS[3]
                self.target[3] = g["min"] + pct / 100.0 * (g["max"] - g["min"])
                self.last_frame = now
                return ["OK"]

            if cmd == "H":
                self.homing = True
                self.streaming = False
                self.target = [j["home"] for j in JOINTS]
                self.add_log("home")
                return ["OK"]

            if cmd == "S":
                self.hold_all()
                self.streaming = False
                self.homing = False
                self.add_log("stop")
                return ["OK"]

            if cmd == "P":
                return ["OK"]

            if cmd == "I":
                parts = []
                for j in JOINTS:
                    parts += [f"{j['min']:.0f}", f"{j['max']:.0f}", f"{j['home']:.0f}"]
                return ["I " + " ".join(parts)]

            return [f"ERR comando desconhecido: {cmd}"]

    def step(self, dt):
        with self.lock:
            now = time.monotonic()
            if self.streaming and now - self.last_frame > WATCHDOG_S:
                self.streaming = False
                self.hold_all()
                self.watchdog_events += 1
                self.add_log("WATCHDOG: 300 ms sem quadro J, servos parados")

            speed = HOME_SPEED if self.homing else MAX_SPEED
            all_home = True
            for i in range(4):
                err = self.target[i] - self.current[i]
                stepv = speed * dt
                self.current[i] = self.target[i] if abs(err) <= stepv else self.current[i] + (stepv if err > 0 else -stepv)
                all_home &= self.current[i] == self.target[i]
            if self.homing and all_home:
                self.homing = False

    def telemetry(self):
        with self.lock:
            return "T " + " ".join(f"{a:.0f}" for a in self.current)

    def snapshot(self):
        with self.lock:
            return {
                "current": self.current,
                "target": self.target,
                "joints": JOINTS,
                "streaming": self.streaming,
                "homing": self.homing,
                "connected": self.connected,
                "client": self.client,
                "note": self.note,
                "page": self.page,
                "frames": self.frames,
                "rate_hz": len(self.frame_times),
                "watchdog_events": self.watchdog_events,
                "log": self.log[-12:],
            }


def serve_stream(arm, read_fn, write_fn, poll_fd, name, wait_for_client=False):
    """Laço de uma conexão: lê linhas, responde e manda T a 10 Hz.

    wait_for_client (serial falsa): numa pty não existe "accept", então até
    chegar o primeiro byte o simulador só repete o READY a cada 1 s, como um
    ESP32 que acabou de ligar e ainda não tem ninguém escutando.
    """
    def mark_connected():
        with arm.lock:
            arm.connected = True
            arm.client = name
        arm.add_log(f"bridge conectada ({name})")

    if not wait_for_client:
        mark_connected()
    write_fn(f"READY {VERSION}\n")

    buf = ""
    talking = not wait_for_client
    last_tel = last_ready = time.monotonic()
    try:
        while True:
            ready, _, _ = select.select([poll_fd], [], [], 0.02)
            if ready:
                data = read_fn()
                if not data:
                    break
                if not talking:
                    talking = True
                    mark_connected()
                buf += data
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    for reply in arm.handle_line(line):
                        write_fn(reply + "\n")
            now = time.monotonic()
            if not talking:
                if now - last_ready >= 1.0:
                    last_ready = now
                    write_fn(f"READY {VERSION}\n")
            elif now - last_tel >= TELEMETRY_S:
                last_tel = now
                write_fn(arm.telemetry() + "\n")
    except OSError as exc:
        arm.add_log(f"conexão caiu: {exc}")
    finally:
        if talking:
            with arm.lock:
                arm.connected = False
            arm.add_log(f"bridge desconectada ({name})")


def tcp_server(arm, host, port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(1)
    print(f"[SIM] protocolo do braço em tcp://{host}:{port}", flush=True)
    while True:
        conn, addr = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        with conn:
            serve_stream(
                arm,
                lambda: conn.recv(512).decode(errors="replace"),
                lambda text: conn.sendall(text.encode()),
                conn,
                f"tcp {addr[0]}:{addr[1]}",
            )


def pty_server(arm):
    import pty
    import tty

    master, slave = pty.openpty()
    tty.setraw(slave)
    path = os.ttyname(slave)
    print(f"[SIM] serial falsa em {path}  (bridge do Mac: --backend serial --device {path})", flush=True)
    while True:
        serve_stream(
            arm,
            lambda: os.read(master, 512).decode(errors="replace"),
            lambda text: os.write(master, text.encode()),
            master,
            f"pty {path}",
            wait_for_client=True,
        )


def physics(arm):
    last = time.monotonic()
    while True:
        time.sleep(UPDATE_S)
        now = time.monotonic()
        arm.step(now - last)
        last = now


def make_http_handler(arm, page_path):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/state"):
                body = json.dumps(arm.snapshot()).encode()
                ctype = "application/json"
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
    parser = argparse.ArgumentParser(description="Simulador do braço MeArm (protocolo SNES_ARM_ESP32)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7878, help="porta do protocolo (adb reverse)")
    parser.add_argument("--http-port", type=int, default=8787, help="porta do painel web")
    parser.add_argument("--pty", action="store_true", help="cria também uma serial falsa")
    args = parser.parse_args()

    page_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "painel.html")

    arm = ArmEmulator()
    threading.Thread(target=physics, args=(arm,), daemon=True).start()
    threading.Thread(target=tcp_server, args=(arm, args.host, args.port), daemon=True).start()
    if args.pty:
        threading.Thread(target=pty_server, args=(arm,), daemon=True).start()

    http = ThreadingHTTPServer((args.host, args.http_port), make_http_handler(arm, page_path))
    print(f"[SIM] painel em http://{args.host}:{args.http_port}", flush=True)
    try:
        http.serve_forever()
    except KeyboardInterrupt:
        print("\n[SIM] encerrado", flush=True)


if __name__ == "__main__":
    main()
