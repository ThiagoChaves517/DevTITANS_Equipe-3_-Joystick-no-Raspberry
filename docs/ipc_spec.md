# Protocolo IPC app → daemon (v1)

Implementação: `app/.../ipc/IpcProtocol.java`, `DaemonClient.java`; `daemon/src/ipc_protocol.*`, `client_session.*`, `main.cpp`. O vetor de contrato é testado nos dois lados (`IpcProtocolTest`, `daemon_tests.cpp`).

## 1. Socket

- `AF_UNIX`, `SOCK_STREAM`, **abstract namespace** `@snesbt_input` (sem arquivo: sem permissões de diretório nem socket órfão após crash).
- Controle de acesso: o daemon lê `SO_PEERCRED` e só aceita os uids passados em `--allow-uid` (o `scripts/deploy.sh` passa o uid do app). Sem `--allow-uid`, aceita qualquer um e registra isso no log.
- Um cliente por vez; uma nova conexão autorizada substitui a anterior (o estado é neutralizado na troca).
- SELinux: a imagem atual está **Permissive**. Em Enforcing, `untrusted_app → shell:unix_stream_socket connectto` será negado — exige política própria (pendente, ver DECISOES.md).

## 2. Mensagem (12 bytes, tamanho fixo)

| Offset | Campo |
|---:|---|
| 0–1 | magic `0x53 0x44` (`SD`) |
| 2 | versão `0x01` |
| 3 | tipo |
| 4–5 | seq u16 LE (diagnóstico) |
| 6–7 | botões u16 LE — mesmo registrador ativo-em-LOW do protocolo BLE |
| 8 | grip 0/1/2 |
| 9 | status (só no HELLO_ACK) |
| 10 | reservado, deve ser 0 |
| 11 | CRC-8/0x07 sobre 0–10 |

Vetor: STATE seq `0x1234`, botões `0xFEFE`, grip 1 → `53 44 01 02 34 12 FE FE 01 00 00 81`.

| Tipo | Direção | Significado |
|---|---|---|
| `0x01` HELLO | app → daemon | primeira mensagem obrigatória |
| `0x81` HELLO_ACK | daemon → app | status `0x00` = dispositivo uinput ativo |
| `0x02` STATE | app → daemon | **estado completo** do controle |
| `0x03` NEUTRAL | app → daemon | libera tudo (botões devem ser `0xFFFF`, grip 0) |

## 3. Regras

- O daemon só abre o socket depois de criar o dispositivo uinput; o app só se considera conectado após um HELLO_ACK válido (timeout 1 s). Portanto, `READY` no app implica dispositivo de entrada criado.
- Mensagens parciais são acumuladas até completar 12 bytes. Qualquer mensagem inválida (`INVALID_MAGIC`, `UNSUPPORTED_VERSION`, `UNKNOWN_TYPE`, `CRC_MISMATCH`, `INVALID_FIELD`) ou STATE antes do HELLO: o daemon registra o motivo, **neutraliza** e fecha a conexão. O app reconecta.
- O app envia STATE a cada mudança e, enquanto houver algo pressionado, reenvia o estado a cada 100 ms (keepalive). Sem nada pressionado não há tráfego.
- Watchdog do daemon (`--client-timeout-ms`, padrão 500 ms): sem mensagens com algo pressionado → neutraliza, uma vez.
- EOF/erro do cliente → neutraliza. SIGTERM/SIGINT → neutraliza e destrói o dispositivo.
- Reconexão: o app envia o estado completo atual logo após o HELLO_ACK.

## 4. Saída uinput

Dispositivo `SNES-BT-Controller`, `BUS_VIRTUAL`, `1209:534E`, layout `Generic.kl` (Android classifica como `GAMEPAD | JOYSTICK`).

| SNES | Evento Linux | Android |
|---|---|---|
| A / B / X / Y | `BTN_A` / `BTN_B` / `BTN_X` / `BTN_Y` | `BUTTON_A/B/X/Y` |
| L / R | `BTN_TL` / `BTN_TR` | `BUTTON_L1/R1` |
| SELECT / START | `BTN_SELECT` / `BTN_START` | `BUTTON_SELECT/START` |
| D-pad | `ABS_HAT0X/Y` (−1/0/1; opostos se anulam) | `MotionEvent` JOYSTICK `AXIS_HAT_X/Y` (o app focado recebe `DPAD_*` sintetizados pelo ViewRootImpl) |
| grip abrir / fechar | `BTN_0` / `BTN_1` | `BUTTON_1` / `BUTTON_2` |

Mapeamento por nome (SNES A → `BTN_A`), não por posição.
