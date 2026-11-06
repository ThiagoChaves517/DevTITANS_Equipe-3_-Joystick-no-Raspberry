#!/usr/bin/env python3
"""Teste de integração no Raspberry (via ADB), sem o ESP32.

Frames v1 reais são injetados no ControllerService (ação INJECT, só em build debug) e entram no
mesmo caminho das notificações BLE: decoder -> core -> watchdog -> IPC -> daemon -> uinput -> Android.
Confere eventos no kernel (getevent), KeyEvents no Android (dumpsys input) e contadores (dumpsys).

Pré-requisito: scripts/deploy.sh já executado.  Uso: python3 scripts/device_test.py
"""
import random
import re
import subprocess
import sys
import threading
import time

PKG = "com.snesbt.receiver"
SVC = PKG + "/.service.ControllerService"
DAEMON = "/data/local/tmp/snes_input_daemon"
DAEMON_LOG = "/data/local/tmp/snes_input_daemon.log"

BIT = {"B": 0, "Y": 1, "SELECT": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7, "A": 8, "X": 9, "L": 10, "R": 11}
# Nome no getevent -l (aliases do kernel) e keycode Android esperado.
KERNEL = {"A": "BTN_GAMEPAD", "B": "BTN_EAST", "X": "BTN_NORTH", "Y": "BTN_WEST", "L": "BTN_TL", "R": "BTN_TR",
          "SELECT": "BTN_SELECT", "START": "BTN_START"}
ANDROID = {"A": "BUTTON_A", "B": "BUTTON_B", "X": "BUTTON_X", "Y": "BUTTON_Y", "L": "BUTTON_L1", "R": "BUTTON_R1",
           "SELECT": "BUTTON_SELECT", "START": "BUTTON_START", "UP": "DPAD_UP", "DOWN": "DPAD_DOWN",
           "LEFT": "DPAD_LEFT", "RIGHT": "DPAD_RIGHT"}
HAT = {"UP": ("ABS_HAT0Y", -1), "DOWN": ("ABS_HAT0Y", 1), "LEFT": ("ABS_HAT0X", -1), "RIGHT": ("ABS_HAT0X", 1)}

results = []


def adb(*args, check=True):
    r = subprocess.run(["adb", "shell", *args], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"adb shell {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout


def crc8(data):
    c = 0
    for x in data:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


_seq = random.randrange(256)


def frame(type_=0x01, p0=0xFF, p1=0xFF, seq=None, version=0x01, sync=(0x53, 0x4E), bad_crc=False):
    global _seq
    if seq is None:
        _seq = (_seq + 1) & 0xFF
        seq = _seq
    f = [sync[0], sync[1], version, type_, seq, p0, p1]
    f.append(crc8(f) ^ (0xFF if bad_crc else 0))
    return bytes(f).hex()


def state(*pressed):
    s = 0xFFFF
    for name in pressed:
        s &= ~(1 << BIT[name])
    return frame(0x01, s & 0xFF, s >> 8)


def grip(g):
    return frame(0x06, g, 0)


def inject(frames, interval_ms=10):
    adb("am", "start-foreground-service", "-n", SVC, "-a", PKG + ".INJECT",
        "--es", "frames", ",".join(frames), "--ei", "interval_ms", str(interval_ms))
    time.sleep(len(frames) * interval_ms / 1000 + 0.25)


def counters():
    out = adb("dumpsys", "activity", "service", SVC)
    c = {}
    for k, v in re.findall(r"([\w)]+)=(-?\d+)\b", out.split("Client:")[-1]):
        c[k] = int(v)  # "lost(seq gaps)=N" vira a chave "gaps)"
    c["_raw"] = out
    m = re.search(r"state=(\w+)", out)
    c["_state"] = m.group(1) if m else "?"
    return c


def event_node():
    out = adb("getevent", "-lp")
    m = re.search(r"add device \d+: (/dev/input/event\d+)\n\s+name:\s+\"SNES-BT-Controller\"", out)
    return m.group(1) if m else None


class Capture:
    """Captura getevent de um nó; eventos como (tempo, tipo, código, valor)."""

    def __init__(self, node):
        self.events = []
        self.p = subprocess.Popen(["adb", "shell", "getevent", "-lt", node], stdout=subprocess.PIPE, text=True)
        threading.Thread(target=self._read, daemon=True).start()
        time.sleep(0.8)  # getevent precisa abrir o nó antes do primeiro evento

    def _read(self):
        for line in self.p.stdout:
            m = re.match(r"\[\s*([\d.]+)\]\s+(\w+)\s+(\w+)\s+(\w+)", line)
            if m and m.group(2) != "EV_SYN":
                v = m.group(4)
                v = {"DOWN": 1, "UP": 0}.get(v, None) if v in ("DOWN", "UP") else int(v, 16)
                if isinstance(v, int) and v >= 0x80000000:
                    v -= 1 << 32
                self.events.append((float(m.group(1)), m.group(2), m.group(3), v))

    def stop(self):
        time.sleep(0.2)
        self.p.terminate()
        return [(c, v) for _, _, c, v in self.events]


def android_events():
    """Fila recente do InputDispatcher: [(eventTime, 'KEY'|'JOYSTICK', ação, keycode)]."""
    out = adb("dumpsys", "input")
    q = out.split("RecentQueue")[1].split("PendingEvent")[0] if "RecentQueue" in out else ""
    ev = [(t, "KEY", a, k) for t, a, k in re.findall(r"KeyEvent\(deviceId=\d+, eventTime=(\d+).*?action=(\w+).*?keyCode=(\w+)\(", q)]
    ev += [(t, "JOYSTICK", a, "") for t, a in re.findall(r"MotionEvent\(deviceId=\d+, eventTime=(\d+), source=JOYSTICK.*?action=(\w+)", q)]
    return ev


def new_android_events(before, expected_count, timeout=1.0):
    """Eventos novos no dispatcher; espera até `expected_count` (o kernel entrega antes do dispatcher)."""
    deadline = time.time() + timeout
    while True:
        ev = _new_android_events(before)
        if len(ev) >= expected_count or time.time() > deadline:
            return ev
        time.sleep(0.1)


def _new_android_events(before):
    # KeyEvents e MotionEvents vêm de buscas separadas; ordena só por tempo (estável dentro de cada tipo).
    seen = set(before)
    return sorted((e for e in android_events() if e not in seen), key=lambda e: int(e[0]))


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(("  OK   " if ok else "  FALHA ") + name + (f"  [{detail}]" if detail else ""))
    return ok


def logcat_since(mark, *tags):
    return subprocess.run(["adb", "logcat", "-d", "-T", mark, "-s", *tags], capture_output=True, text=True).stdout


def now_mark():
    return adb("date", "'+%m-%d %H:%M:%S.000'").strip()


# ---------------------------------------------------------------- cenários

def t_buttons(node):
    print("\n[1] Cada botão individualmente (kernel + Android)")
    for name in ["A", "B", "X", "Y", "L", "R", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT"]:
        before = android_events()
        cap = Capture(node)
        inject([state(name)] * 3 + [state()] * 2)
        ev = cap.stop()
        andr = [(k, a, c) for _, k, a, c in new_android_events(before, 2)]
        if name in KERNEL:
            exp = [(KERNEL[name], 1), (KERNEL[name], 0)]
        else:
            axis, v = HAT[name]
            exp = [(axis, v), (axis, 0)]
        check(f"{name}: kernel {exp}", ev == exp, f"obtido {ev}")
        if name in KERNEL:
            check(f"{name}: Android KeyEvent {ANDROID[name]} DOWN/UP",
                  andr == [("KEY", "DOWN", ANDROID[name]), ("KEY", "UP", ANDROID[name])], f"obtido {andr}")
        else:
            check(f"{name}: Android MotionEvent JOYSTICK (AXIS_HAT)", len(andr) == 2 and all(k == "JOYSTICK" for k, _, _ in andr),
                  f"obtido {andr}")


def t_combo_and_grip(node):
    print("\n[2] Combinação simultânea, diagonal e garra")
    cap = Capture(node)
    inject([state("A", "B")] * 2 + [state("A")] * 2 + [state()] * 2)
    ev = cap.stop()
    check("A+B no mesmo frame -> 2 DOWN; solta B; solta A",
          sorted(ev[:2]) == [("BTN_EAST", 1), ("BTN_GAMEPAD", 1)] and ev[2:] == [("BTN_EAST", 0), ("BTN_GAMEPAD", 0)], f"{ev}")
    cap = Capture(node)
    inject([state("UP", "RIGHT")] * 2 + [state("LEFT", "RIGHT")] * 2 + [state()] * 2)
    ev = cap.stop()
    check("UP+RIGHT diagonal; LEFT+RIGHT se anulam", ev[:2] == [("ABS_HAT0X", 1), ("ABS_HAT0Y", -1)]
          and ("ABS_HAT0X", 0) in ev and ("ABS_HAT0Y", 0) in ev, f"{ev}")
    before = android_events()
    cap = Capture(node)
    inject([grip(1)] * 2 + [grip(2)] * 2 + [grip(0)] * 2)
    ev = cap.stop()
    # getevent -l exibe BTN_0 pelo alias BTN_MISC.
    check("GRIP abrir/fechar -> BTN_0/BTN_1", ev == [("BTN_MISC", 1), ("BTN_MISC", 0), ("BTN_1", 1), ("BTN_1", 0)], f"{ev}")
    keys = [(a, c) for _, _, a, c in new_android_events(before, 4)]
    check("GRIP no Android -> BUTTON_1/BUTTON_2",
          keys == [("DOWN", "BUTTON_1"), ("UP", "BUTTON_1"), ("DOWN", "BUTTON_2"), ("UP", "BUTTON_2")], f"{keys}")


def t_invalid(node):
    print("\n[3] Frames inválidos não geram eventos e são contados por motivo")
    global _seq
    cases = {
        "CRC_MISMATCH": frame(0x01, 0xFF, 0xFE, bad_crc=True),
        "INVALID_SYNC": frame(0x01, 0xFF, 0xFE, sync=(0x53, 0x4F)),
        "INVALID_LENGTH": "fffe",  # formato antigo de 2 bytes
        "UNSUPPORTED_VERSION": frame(0x01, 0xFF, 0xFE, version=0x02),
        "UNKNOWN_TYPE": frame(0x42, 0xFF, 0xFE),
    }
    for reason, f in cases.items():
        before = counters()
        cap = Capture(node)
        inject([f])
        ev = cap.stop()
        after = counters()
        check(f"{reason}: sem eventos e contador +1", ev == [] and after[reason] == before[reason] + 1,
              f"eventos={ev} {before[reason]}->{after[reason]}")
    before = counters()
    base = frame(0x01, 0xFF, 0xFF)
    seq = int(base[8:10], 16)
    dup = frame(0x01, 0xFF, 0xFE, seq=seq)  # mesma sequência, pressionando A
    cap = Capture(node)
    inject([base, dup])
    ev = cap.stop()
    after = counters()
    check("SEQUENCE_ERROR (duplicado com A pressionado): sem eventos",
          ev == [] and after["SEQUENCE_ERROR"] == before["SEQUENCE_ERROR"] + 1, f"eventos={ev}")
    before = counters()
    cap = Capture(node)
    inject([frame(0x03, 0, 0), frame(0x07, 1, 1)])  # PING e ACCEL_STATE
    ev = cap.stop()
    after = counters()
    check("Tipos válidos não usados (PING, ACCEL_STATE): ignorados e contados",
          ev == [] and after["ignoredTypes"] == before["ignoredTypes"] + 2, f"eventos={ev}")
    before = counters()
    cap = Capture(node)
    inject([grip(3)])
    ev = cap.stop()
    after = counters()
    check("GRIP fora do domínio (3): payload inválido, sem eventos",
          ev == [] and after["invalidPayload"] == before["invalidPayload"] + 1, f"eventos={ev}")


def t_watchdog(node):
    print("\n[4] Watchdog do app: B segurado e frames param")
    before = counters()
    mark = now_mark()
    cap = Capture(node)
    inject([state("B")] * 30, interval_ms=10)  # 300 ms a ~100 Hz, como o firmware
    time.sleep(0.5)
    ev = cap.stop()
    log = logcat_since(mark, "SnesBtReceiver")
    m = re.search(r"watchdog: (\d+) ms sem frame", log)
    after = counters()
    check("B pressionado e liberado uma única vez", ev == [("BTN_EAST", 1), ("BTN_EAST", 0)], f"{ev}")
    check("neutralização ocorreu entre 100 e 150 ms após o último frame",
          bool(m) and 100 <= int(m.group(1)) <= 150, m.group(0) if m else "sem log do watchdog")
    check("watchdogExpirations +1", after["watchdogExpirations"] == before["watchdogExpirations"] + 1)
    cap = Capture(node)
    time.sleep(0.5)
    check("sem liberações repetidas depois do neutro", cap.stop() == [])


def t_determinism(node):
    print("\n[5] Sequência aleatória de 400 frames a 10 ms: transições exatas e estado final")
    rnd = random.Random(1234)
    names = list(KERNEL)
    states, frames, expected = [], [], 0
    prev = set()
    for _ in range(400):
        cur = prev if rnd.random() < 0.6 else set(n for n in names if rnd.random() < 0.3)
        expected += len(cur ^ prev)
        states.append(cur)
        frames.append(state(*cur))
        prev = cur
    expected += len(prev)  # liberação final
    frames.append(state())
    before = counters()
    cap = Capture(node)
    inject(frames, interval_ms=10)
    ev = cap.stop()
    after = counters()
    check(f"{expected} transições esperadas", len(ev) == expected, f"obtidas {len(ev)}")
    downs = {}
    for c, v in ev:
        downs[c] = downs.get(c, 0) + (1 if v else -1)
    check("todos os botões soltos no fim (DOWN == UP por botão)", all(v == 0 for v in downs.values()), f"{downs}")
    check("401 frames válidos, nenhuma lacuna de sequência",
          after["valid"] - before["valid"] == 401 and after["gaps)"] == before["gaps)"],
          f"valid +{after['valid'] - before['valid']}, lacunas +{after['gaps)'] - before['gaps)']}")


def t_daemon_restart():
    print("\n[6] Queda e retorno do daemon com A segurado (frames contínuos, como o firmware)")
    uid = re.search(r"uid:(\d+)", adb("pm", "list", "packages", "-U", PKG)).group(1)
    # 4 s de A pressionado a 10 ms, sem esperar: o daemon cai e volta no meio da sequência.
    adb("am", "start-foreground-service", "-n", SVC, "-a", PKG + ".INJECT", "--es", "frames",
        ",".join([state("A")] * 400), "--ei", "interval_ms", "10")
    time.sleep(0.5)
    adb(f"pkill -f {DAEMON}", check=False)  # adb shell junta os argumentos: passar a linha inteira
    time.sleep(0.7)
    c = counters()
    check("serviço detecta perda do daemon", c["_state"] in ("RECOVERING", "DAEMON_CONNECTING"), f"state={c['_state']}")
    adb(f"nohup {DAEMON} --allow-uid {uid} > {DAEMON_LOG} 2>&1 &")
    node = None
    for _ in range(20):
        time.sleep(0.1)
        node = event_node()
        if node:
            break
    cap = Capture(node)
    time.sleep(3.5)  # reconexão (backoff <= 2 s) + fim dos frames + watchdog
    ev = cap.stop()
    c = counters()
    check("novo dispositivo: app reconecta, ressincroniza A e o watchdog solta no fim",
          ev == [("BTN_GAMEPAD", 1), ("BTN_GAMEPAD", 0)], f"{ev}")
    check("ipc up=true após reconexão", "ipc up=true" in c["_raw"])
    return node


def t_app_killed(node):
    print("\n[7] App morto com botão pressionado: daemon libera por EOF")
    cap = Capture(node)
    adb("am", "start-foreground-service", "-n", SVC, "-a", PKG + ".INJECT", "--es", "frames",
        ",".join([state("X")] * 40), "--ei", "interval_ms", "10")
    time.sleep(0.15)
    adb("am", "force-stop", PKG)
    time.sleep(0.8)
    ev = cap.stop()
    log = adb("tail", "-5", DAEMON_LOG)
    check("X pressionado e liberado pelo daemon", ev == [("BTN_NORTH", 1), ("BTN_NORTH", 0)], f"{ev}")
    check("daemon registrou EOF e liberação", "EOF" in log and "liberados" in log, log.strip().splitlines()[-1] if log else "")
    adb("am", "start-foreground-service", "-n", SVC)
    time.sleep(2)
    check("serviço reinicia e reconecta ao daemon", "ipc up=true" in counters()["_raw"])


def t_latency():
    print("\n[8] Latência no app (notificação -> escrita no socket do daemon)")
    m = re.search(r"latencyUs\(last/avg/max\)=(\d+)/(\d+)/(\d+)", counters()["_raw"])
    if m:
        avg, mx = int(m.group(2)), int(m.group(3))
        check(f"latência média {avg} us, máxima {mx} us (< 5 ms em média)", avg < 5000)


def main():
    node = event_node()
    if not node or "ipc up=true" not in counters()["_raw"]:
        print("Daemon/serviço não estão ativos. Rode scripts/deploy.sh antes.")
        return 2
    print(f"Dispositivo: {node}; seq inicial {_seq}")
    t_buttons(node)
    t_combo_and_grip(node)
    t_invalid(node)
    t_watchdog(node)
    t_determinism(node)
    node = t_daemon_restart()
    t_latency()
    t_app_killed(node)
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} verificações OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
