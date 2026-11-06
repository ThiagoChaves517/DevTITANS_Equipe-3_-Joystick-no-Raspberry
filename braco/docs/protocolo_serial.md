# Protocolo Raspberry → ESP32 do braço (v1)

Implementação: `braco/daemon/src/forwarder.*` (envio) e `braco/firmware_esp32/SNES_ARM_ESP32/FrameProtocol.*`
(quadro e decodificador, compartilhados pelo firmware, pelo daemon e pelos testes). Testes:
`braco/daemon/tests/daemon_tests.cpp`.

## 1. Transporte

| Item | Valor |
|---|---|
| Meio | Serial USB do ESP32 do braço (`/dev/ttyUSB*` com CP2102, `/dev/ttyACM*` com USB nativo) |
| Formato | 115200 8N1, sem controle de fluxo, modo raw |
| Sentido | **Só Raspberry → braço.** O braço não responde; o que ele escreve na serial são logs `[ARM] ...` para humanos (o daemon descarta, ou mostra com `--echo`) |
| Simulador | O mesmo fluxo de bytes por TCP (`--device tcp:127.0.0.1:7878`) |

## 2. Quadro

**O mesmo quadro v1 que o joystick envia ao Android** (`driver/docs/protocol_spec.md`). O braço recebe
exatamente os 16 bits que o controle mandou.

| Offset | Campo | Valor |
|---:|---|---|
| 0 | SYNC1 | `0x53` (`S`) |
| 1 | SYNC2 | `0x4E` (`N`) |
| 2 | Versão | `0x01` |
| 3 | Tipo | `0x01` CONTROLLER_STATE (único tipo enviado ao braço) |
| 4 | Sequência | u8, +1 a cada quadro enviado (diagnóstico) |
| 5 | Payload 0 | byte baixo do registrador SNES |
| 6 | Payload 1 | byte alto do registrador SNES |
| 7 | CRC-8 | poly `0x07`, init `0x00`, sobre os bytes 0–6 |

Vetores (testados em `daemon_tests.cpp`):
- Nada pressionado, seq 0: `53 4E 01 01 00 FF FF 6F`.
- CRC de `"123456789"` = `0xF4`.

## 3. Registrador (16 bits, ativo em LOW)

| Bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12–15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Botão | B | Y | SELECT | START | UP | DOWN | LEFT | RIGHT | A | X | L | R | sempre 1 |

Na Raspberry, o registrador é remontado a partir dos eventos do gamepad `SNES-BT-Controller`. É o
caminho inverso do `ControllerDevice` do `snes_input_daemon`:

| Evento Linux | Bit |
|---|---|
| `BTN_B` / `BTN_Y` / `BTN_SELECT` / `BTN_START` | 0 / 1 / 2 / 3 |
| `ABS_HAT0Y` −1 / +1 | 4 (UP) / 5 (DOWN) |
| `ABS_HAT0X` −1 / +1 | 6 (LEFT) / 7 (RIGHT) |
| `BTN_A` / `BTN_X` / `BTN_TL` / `BTN_TR` | 8 / 9 / 10 / 11 |
| `BTN_0` / `BTN_1` (garra pelo acelerômetro) | **não fazem parte dos 16 bits**: ignorados |

## 4. Quando a Raspberry envia

Mesma política do app do Raphael para o daemon dele (`driver/docs/ipc_spec.md §3`):

- **Estado completo** em todo quadro.
- Envia **na mudança**.
- **Enquanto houver botão pressionado**, reenvia a cada **100 ms** (keepalive, `--keepalive-ms`).
- **Nada pressionado:** sem tráfego.
- **Link novo**, quando o ESP32 reinicia ao abrir a serial ou o cabo volta: envia o estado atual depois de
  `--boot-wait-ms`. Padrão de 1500 ms; na TCP não há espera.
- **Gamepad sumiu**, porque o `snes_input_daemon` parou ou foi morto: envia o neutro `0xFFFF` na hora.
- **Encerramento** por SIGINT/SIGTERM: envia o neutro antes de sair.

## 5. O que o ESP32 faz com os 16 bits

Wiki "Braço Robótico ‐ Semana 09\09‐21\10" (William) e o fluxograma em `resources/`:

| Botão | Servo | Direção |
|---|---|---|
| ← / → | base | −1 / +1 |
| ↑ / ↓ | ombro | +1 / −1 |
| B / Y | cotovelo | +1 / −1 |
| A / X | garra | +1 (abre) / −1 (fecha) |
| Start | todos | 0 enquanto pressionado ("parar os movimentos") |
| Select, L, R | — | nada no braço |

- Os dois sentidos juntos se anulam.
- A cada **25 ms**, cada servo anda **1°** na sua direção, dentro de `min..max`.
- **250 ms sem quadro válido** zera as direções. Isso cobre o cabo arrancado ou o daemon travado com um botão pressionado.

## 6. Recepção no ESP32 (`FrameDecoder`)

1. Descarta bytes até achar `53 4E`.
2. Com 8 bytes, confere a versão (1), o tipo conhecido (`0x01–0x0A`) e o CRC.
3. Se for inválido, conta o erro e recomeça a busca no byte seguinte. Um byte perdido custa no máximo um quadro.
4. Tipos válidos diferentes de CONTROLLER_STATE são ignorados e não renovam o watchdog.

A sequência é só diagnóstico. Como cada quadro traz o estado completo, um quadro perdido ou duplicado não muda o resultado.
