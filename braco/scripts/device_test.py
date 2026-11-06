#!/usr/bin/env python3
"""Teste de integração no Raspberry (via ADB), sem joystick e sem o ESP32 do braço.

Monta o caminho real dos dois drivers, com uma instância de TESTE do daemon do Raphael
(socket e nome próprios, para não mexer num app/daemon que esteja rodando):

    ipc_probe ──IPC──▶ snes_input_daemon ──uinput──▶ /dev/input/eventX
        ──evdev──▶ snes_arm_daemon ──serial (pty)──▶ frame_sniffer ("ESP32 falso")

e confere que cada estado de 16 bits enviado pelo ipc_probe chega igual ao "braço",
com keepalive enquanto há botão pressionado e estado neutro quando o controle some.

Uso:  python3 braco/scripts/device_test.py      (compila e envia tudo antes)
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REMOTE = "/data/local/tmp"
SOCKET = "snesbt_input_armtest"
NAME = "SNES-ARM-TEST"

# Estados (ativo em LOW) e o que o braço faz com cada um (wiki do William).
STEPS = [
    (0xFEFF, "A: abre a garra"),
    (0xFFFF, "solta"),
    (0xFFBF, "LEFT: base -"),
    (0xFF7F, "RIGHT: base +"),
    (0xFFEF, "UP: ombro sobe"),
    (0xFFDF, "DOWN: ombro desce"),
    (0xFEFE, "A + B: garra e cotovelo"),
    (0xFFFD, "Y: cotovelo desce"),
    (0xFDFF, "X: fecha a garra"),
    (0xFFF7, "START: parado"),
    (0xFFFF, "solta"),
]
HOLD_MS = 400
results = []


def sh(cmd, check=True):
    r = subprocess.run(["adb", "shell", cmd], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"adb shell {cmd}: {r.stderr.strip()}")
    return r.stdout


def check(name, ok, detail=""):
    results.append(ok)
    print(f"[{'OK' if ok else 'FALHA'}] {name}" + (f"\n        {detail}" if detail and not ok else ""))


def find_ndk():
    sdk = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
    if os.environ.get("ANDROID_NDK_HOME"):
        return Path(os.environ["ANDROID_NDK_HOME"])
    # Mais nova com toolchain (o daemon/build.sh da raiz pega a mais nova mesmo se incompleta).
    for ndk in sorted((sdk / "ndk").glob("*"), key=lambda p: [int(x) for x in re.findall(r"\d+", p.name)], reverse=True):
        if (ndk / "toolchains/llvm/prebuilt").is_dir():
            return ndk
    sys.exit("NDK não encontrado; defina ANDROID_NDK_HOME")


def build_and_push():
    ndk = find_ndk()
    env = {**os.environ, "ANDROID_NDK_HOME": str(ndk)}
    subprocess.run([str(ROOT / "braco/daemon/build.sh")], check=True, capture_output=True, env=env)
    subprocess.run([str(ROOT / "daemon/build.sh")], check=True, capture_output=True, env=env)
    cxx = next((ndk / "toolchains/llvm/prebuilt").iterdir()) / "bin/aarch64-linux-android34-clang++"
    subprocess.run([str(cxx), "-std=c++17", "-O2", "-static-libstdc++", str(ROOT / "daemon/tools/ipc_probe.cpp"),
                    str(ROOT / "daemon/src/ipc_protocol.cpp"), "-o", str(ROOT / "daemon/build/ipc_probe")], check=True)
    subprocess.run(["adb", "root"], capture_output=True)
    subprocess.run(["adb", "wait-for-device"], check=True, timeout=30)
    files = [ROOT / "braco/daemon/build/snes_arm_daemon", ROOT / "braco/daemon/build/frame_sniffer",
             ROOT / "daemon/build/snes_input_daemon", ROOT / "daemon/build/ipc_probe"]
    subprocess.run(["adb", "push", *map(str, files), REMOTE + "/"], check=True, capture_output=True)
    sh(f"chmod 755 {REMOTE}/snes_arm_daemon {REMOTE}/frame_sniffer {REMOTE}/snes_input_daemon {REMOTE}/ipc_probe")


def cleanup():
    sh(f"pkill -f '{REMOTE}/frame_sniffer'; pkill -f 'snes_arm_daemon --device /dev/pts'; "
       f"pkill -f 'snes_input_daemon --socket {SOCKET}'; pkill -f 'ipc_probe --socket {SOCKET}'", check=False)


def main():
    print("== compilar e enviar")
    build_and_push()
    cleanup()

    print("== subir: snes_input_daemon (teste) -> snes_arm_daemon -> frame_sniffer")
    sh(f"cd {REMOTE} && (./snes_input_daemon --socket {SOCKET} --name {NAME} > input_test.log 2>&1 &)")
    sh(f"cd {REMOTE} && (./frame_sniffer --duration-ms 30000 > sniffer.log 2>&1 &)")
    time.sleep(1.0)
    m = re.search(r"PTY (\S+)", sh(f"cat {REMOTE}/sniffer.log"))
    check("frame_sniffer criou a pseudo-serial", bool(m))
    if not m:
        return
    sh(f"cd {REMOTE} && (./snes_arm_daemon --device {m.group(1)} --boot-wait-ms 0 --name {NAME} --verbose "
       f"> arm_test.log 2>&1 &)")
    time.sleep(1.5)
    arm_log = sh(f"cat {REMOTE}/arm_test.log")
    check("snes_arm_daemon achou o gamepad criado pelo snes_input_daemon", f'"{NAME}"' in arm_log, arm_log)

    print("== ipc_probe envia os estados (passando pelo daemon do Raphael)")
    probe = sh(f"{REMOTE}/ipc_probe --socket {SOCKET} --hold-ms {HOLD_MS} " + " ".join(f"{s:04X}" for s, _ in STEPS))
    check("HELLO_ACK do snes_input_daemon", "HELLO_ACK ok" in probe, probe)

    print("== segura A por 1 s (keepalive)")
    sh(f"{REMOTE}/ipc_probe --socket {SOCKET} --hold-ms 1000 FEFF")

    print("== X pressionado e kill -9 no snes_input_daemon (o gamepad some sem soltar o botão)")
    sh(f"cd {REMOTE} && (./ipc_probe --socket {SOCKET} --hold-ms 5000 FDFF > /dev/null 2>&1 &)")
    time.sleep(0.8)
    sh(f"pkill -9 -f 'snes_input_daemon --socket {SOCKET}'", check=False)
    time.sleep(0.8)
    sh(f"pkill -f '{REMOTE}/frame_sniffer' ; sleep 0.3", check=False)

    sniff = sh(f"cat {REMOTE}/sniffer.log")
    arm_log = sh(f"cat {REMOTE}/arm_test.log")
    print(sniff)

    received = [int(x, 16) for x in re.findall(r"estado=0x([0-9A-F]{4})", sniff)]
    expected = [0xFFFF] + [s for s, _ in STEPS] + [0xFEFF, 0xFFFF, 0xFDFF, 0xFFFF]
    check("sequência de estados recebida pelo \"braço\" é exatamente a enviada", received == expected,
          f"esperado {[f'{x:04X}' for x in expected]}\n        recebido {[f'{x:04X}' for x in received]}")

    # Keepalive: entre as linhas do A segurado por 1 s não aparecem repetidos (o sniffer só imprime
    # mudanças), então conta pelos números de sequência: 1 s / 100 ms ≈ 10 quadros.
    seqs = [(int(s), int(e, 16)) for s, e in re.findall(r"seq=\s*(\d+) estado=0x([0-9A-F]{4})", sniff)]
    hold = [i for i, (_, st) in enumerate(seqs) if st == 0xFEFF]
    if len(hold) >= 2:
        i = hold[-1]
        gap = (seqs[i + 1][0] - seqs[i][0]) % 256 if i + 1 < len(seqs) else 0
        check("keepalive a cada 100 ms com A segurado 1 s (8 a 13 quadros)", 8 <= gap <= 13, f"{gap} quadros")
    else:
        check("keepalive a cada 100 ms com A segurado 1 s", False, "estado FEFF não apareceu duas vezes")

    check("nenhum quadro inválido na serial", "invalidos=0 " in sniff, sniff[-200:])
    check("gamepad sumiu com X pressionado -> snes_arm_daemon mandou neutro", "controle perdido" in arm_log, arm_log)

    sh(f"rm -f {REMOTE}/sniffer.log {REMOTE}/arm_test.log {REMOTE}/input_test.log", check=False)
    print(f"\n{sum(results)}/{len(results)} verificações OK")


if __name__ == "__main__":
    try:
        main()
    finally:
        cleanup()
    sys.exit(0 if results and all(results) else 1)
