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
REMOTE = "/data/local/tmp/armtest"  # pasta própria: não sobrescreve o snes_input_daemon/snes_arm_daemon instalados
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
    r = subprocess.run(["adb", "shell", cmd], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if check and r.returncode != 0:
        raise RuntimeError(f"adb shell {cmd}: {r.stderr.strip()}")
    return r.stdout


def check(name, ok, detail=""):
    results.append(ok)
    print(f"[{'OK' if ok else 'FALHA'}] {name}" + (f"\n        {detail}" if detail and not ok else ""))


def find_ndk():
    if os.environ.get("ANDROID_NDK_HOME"):
        return Path(os.environ["ANDROID_NDK_HOME"])
    # SDK do Android Studio no Mac, no Linux e no Windows.
    sdks = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"), Path.home() / "Library/Android/sdk",
            Path.home() / "Android/Sdk", Path(os.environ.get("LOCALAPPDATA", "")) / "Android/Sdk"]
    for sdk in (Path(s) for s in sdks if s):
        # Mais nova com toolchain (uma instalação incompleta não tem toolchains/llvm).
        for ndk in sorted((sdk / "ndk").glob("*"), key=lambda p: [int(x) for x in re.findall(r"\d+", p.name)],
                          reverse=True):
            if (ndk / "toolchains/llvm/prebuilt").is_dir():
                return ndk
    sys.exit("NDK não encontrado; defina ANDROID_NDK_HOME")


def build_and_push():
    # Compila direto com o clang++ do NDK (--target), sem os build.sh: assim funciona também no Windows,
    # onde o atalho aarch64-linux-android34-clang++ é um .cmd. Mesmas flags dos dois build.sh.
    ndk = find_ndk()
    cxx = next((ndk / "toolchains/llvm/prebuilt").iterdir()) / "bin/clang++"
    flags = ["--target=aarch64-linux-android34", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
             "-static-libstdc++"]
    fw = ROOT / "braco/firmware_esp32/SNES_ARM_ESP32"
    arm = ROOT / "braco/daemon"
    raph = ROOT / "daemon"
    (arm / "build").mkdir(exist_ok=True)
    (raph / "build").mkdir(exist_ok=True)
    jobs = [
        ([*sorted((arm / "src").glob("*.cpp")), fw / "FrameProtocol.cpp"], ["-I", fw, "-llog"], arm / "build/snes_arm_daemon"),
        ([arm / "tools/frame_sniffer.cpp", fw / "FrameProtocol.cpp"], ["-I", fw], arm / "build/frame_sniffer"),
        (sorted((raph / "src").glob("*.cpp")), ["-llog"], raph / "build/snes_input_daemon"),
        ([raph / "tools/ipc_probe.cpp", raph / "src/ipc_protocol.cpp"], [], raph / "build/ipc_probe"),
    ]
    for srcs, extra, out in jobs:
        subprocess.run([str(cxx), *flags, *map(str, srcs), *map(str, extra), "-o", str(out)], check=True)

    subprocess.run(["adb", "root"], capture_output=True)
    subprocess.run(["adb", "wait-for-device"], check=True, timeout=30)
    sh(f"mkdir -p {REMOTE}")
    files = [arm / "build/snes_arm_daemon", arm / "build/frame_sniffer", raph / "build/snes_input_daemon",
             raph / "build/ipc_probe"]
    subprocess.run(["adb", "push", *map(str, files), REMOTE + "/"], check=True, capture_output=True)
    sh(f"chmod 755 {REMOTE}/snes_arm_daemon {REMOTE}/frame_sniffer {REMOTE}/snes_input_daemon {REMOTE}/ipc_probe")


def cleanup():
    # "[x]..." no padrão: o pkill -f não casa com o próprio "sh -c" do adb (que contém o texto do padrão
    # e seria morto antes de chegar aos outros pkill).
    sh(f"pkill -f '[f]rame_sniffer --duration-ms'; pkill -f '[s]nes_arm_daemon --device /dev/pts'; "
       f"pkill -f '[s]nes_input_daemon --socket {SOCKET}'; pkill -f '[i]pc_probe --socket {SOCKET}'; true",
       check=False)


def run_chain(protocol):
    """Sobe a cadeia completa com o protocolo dado, roda os passos e devolve (sniffer, arm_log, cpu)."""
    print(f"\n######## protocolo {protocol} ########")
    cleanup()
    print("== subir: snes_input_daemon (teste) -> snes_arm_daemon -> frame_sniffer (ESP32 falso)")
    # O snes_input_daemon solta os botões após 500 ms sem mensagem do cliente (watchdog, 05/11). O app
    # reenvia a cada 100 ms, mas o ipc_probe manda cada estado uma vez só: a instância de teste usa 3 s.
    sh(f"cd {REMOTE} && (./snes_input_daemon --socket {SOCKET} --name {NAME} --client-timeout-ms 3000 "
       f"> input_test.log 2>&1 &)")
    william = " --william" if protocol == "william" else ""
    sh(f"cd {REMOTE} && ({REMOTE}/frame_sniffer --duration-ms 30000{william} > sniffer.log 2>&1 &)")
    time.sleep(1.0)
    m = re.search(r"PTY (\S+)", sh(f"cat {REMOTE}/sniffer.log"))
    check(f"[{protocol}] frame_sniffer criou a pseudo-serial", bool(m))
    if not m:
        return None
    sh(f"cd {REMOTE} && (./snes_arm_daemon --device {m.group(1)} --protocolo {protocol} --boot-wait-ms 0 "
       f"--name {NAME} --verbose > arm_test.log 2>&1 &)")
    time.sleep(1.5)
    arm_log = sh(f"cat {REMOTE}/arm_test.log")
    check(f"[{protocol}] snes_arm_daemon achou o gamepad criado pelo snes_input_daemon", f'"{NAME}"' in arm_log,
          arm_log)

    print("== ipc_probe envia os estados (passando pelo daemon do Raphael)")
    probe = sh(f"{REMOTE}/ipc_probe --socket {SOCKET} --hold-ms {HOLD_MS} " + " ".join(f"{s:04X}" for s, _ in STEPS))
    check(f"[{protocol}] HELLO_ACK do snes_input_daemon", "HELLO_ACK ok" in probe, probe)

    print("== segura A por 1 s (keepalive)")
    sh(f"{REMOTE}/ipc_probe --socket {SOCKET} --hold-ms 1000 FEFF")

    print("== X pressionado e kill -9 no snes_input_daemon (o gamepad some sem soltar o botão)")
    sh(f"cd {REMOTE} && (./ipc_probe --socket {SOCKET} --hold-ms 5000 FDFF > /dev/null 2>&1 &)")
    time.sleep(0.8)
    sh(f"pkill -9 -f '[s]nes_input_daemon --socket {SOCKET}'", check=False)
    time.sleep(0.8)

    print("== ESP32 falso desliga (como o cabo do braço saindo): o daemon tem que largar a serial sem girar a CPU")
    sh("pkill -f '[f]rame_sniffer --duration-ms'; sleep 1.5", check=False)
    pid = sh("pgrep -f '[s]nes_arm_daemon --device /dev/pts'", check=False).split()
    cpu = None
    if pid:
        top = sh(f"top -b -n 1 -d 1 -p {pid[0]}", check=False).strip().splitlines()
        fields = top[-1].split() if top else []
        cpu = float(fields[8]) if len(fields) > 8 and re.match(r"^[\d.]+$", fields[8]) else None

    sniff = sh(f"cat {REMOTE}/sniffer.log")
    arm_log = sh(f"cat {REMOTE}/arm_test.log")
    print(sniff)

    check(f"[{protocol}] gamepad sumiu com X pressionado -> snes_arm_daemon soltou tudo",
          "controle perdido" in arm_log, arm_log)
    check(f"[{protocol}] serial do braço caiu -> snes_arm_daemon largou o link",
          "link com o braço perdido" in arm_log, arm_log)
    check(f"[{protocol}] serial do braço caiu -> snes_arm_daemon não gira a CPU (< 20%)",
          cpu is not None and cpu < 20, f"CPU={cpu}")
    sh(f"rm -f {REMOTE}/sniffer.log {REMOTE}/arm_test.log {REMOTE}/input_test.log", check=False)
    return sniff


def check_frame(sniff):
    received = [int(x, 16) for x in re.findall(r"estado=0x([0-9A-F]{4})", sniff)]
    expected = [0xFFFF] + [s for s, _ in STEPS] + [0xFEFF, 0xFFFF, 0xFDFF, 0xFFFF]
    check("[frame] sequência de estados recebida pelo \"braço\" é exatamente a enviada", received == expected,
          f"esperado {[f'{x:04X}' for x in expected]}\n        recebido {[f'{x:04X}' for x in received]}")

    # Keepalive: entre as linhas do A segurado por 1 s não aparecem repetidos (o sniffer só imprime
    # mudanças), então conta pelos números de sequência: 1 s / 100 ms ≈ 10 quadros.
    seqs = [(int(s), int(e, 16)) for s, e in re.findall(r"seq=\s*(\d+) estado=0x([0-9A-F]{4})", sniff)]
    hold = [i for i, (_, st) in enumerate(seqs) if st == 0xFEFF]
    if len(hold) >= 2:
        i = hold[-1]
        gap = (seqs[i + 1][0] - seqs[i][0]) % 256 if i + 1 < len(seqs) else 0
        check("[frame] keepalive a cada 100 ms com A segurado 1 s (8 a 13 quadros)", 8 <= gap <= 13, f"{gap} quadros")
    else:
        check("[frame] keepalive a cada 100 ms com A segurado 1 s", False, "estado FEFF não apareceu duas vezes")
    check("[frame] nenhum quadro inválido na serial", "invalidos=0 " in sniff, sniff[-200:])


def check_william(sniff):
    # Uma "rajada" por botão segurado: solto não manda nada, então os 0xFFFF não aparecem.
    bursts = [(int(v), int(n), int(ms)) for v, n, ms in
              re.findall(r"linha=(\d+)\s.*?x(\d+) linhas em (\d+) ms", sniff)]
    values = [v for v, _, _ in bursts]
    expected = [~s & 0x0FFF for s, _ in STEPS if s != 0xFFFF] + [0x100, 0x200]
    check("[william] sequência de botões recebida pelo \"braço\" é exatamente a enviada", values == expected,
          f"esperado {expected}\n        recebido {values}")
    if values == expected:
        hold_a = bursts[-2]
        check("[william] A segurado 1 s -> ~1 linha a cada 30 ms (28 a 36 linhas)", 28 <= hold_a[1] <= 36,
              f"{hold_a[1]} linhas em {hold_a[2]} ms")
        x = bursts[-1]
        check("[william] gamepad sumiu com X pressionado -> as linhas pararam (rajada < 2 s; o probe segurava 5 s)", x[2] < 2000,
              f"{x[1]} linhas em {x[2]} ms (ipc_probe segurava 5 s)")
    check("[william] nenhuma linha inválida e nunca 2 linhas em < 25 ms (o firmware lê 1 a cada 25 ms)",
          "invalidas=0 " in sniff and "abaixo_de_25ms=0" in sniff, sniff[-200:])


def main():
    print("== compilar e enviar")
    build_and_push()
    for protocol, verify in (("frame", check_frame), ("william", check_william)):
        sniff = run_chain(protocol)
        if sniff is not None:
            verify(sniff)
    print(f"\n{sum(results)}/{len(results)} verificações OK")


if __name__ == "__main__":
    try:
        main()
    finally:
        cleanup()
    sys.exit(0 if results and all(results) else 1)
