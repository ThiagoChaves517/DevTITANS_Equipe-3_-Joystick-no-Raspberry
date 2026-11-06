# Protocolo ESP32 → Android (v1)

Fonte da verdade: firmware oficial `../firmware/SNES_BT_Controller` como está na máquina de desenvolvimento (inclui alterações ainda não commitadas no git do firmware; a versão commitada enviava frames de 2 bytes e não vale como referência). Arquivos: `FrameProtocol.h/.cpp`, `TransportManager.cpp`, `BleService.cpp`, `SnesState.h`. Conferido em 2026-10-21; compila com core ESP32 3.3.7 (Bluedroid). Implementação: `app/.../protocol/`. Testes: `FrameDecoderTest`.

## 1. Transporte BLE

| Item | Valor |
|---|---|
| Nome anunciado | `SNES-BT-Controller` |
| Serviço | `8b7e0001-5a9d-4c31-9f20-7d6a8e42b101` |
| Característica de estado | `8b7e0002-5a9d-4c31-9f20-7d6a8e42b101` (READ + NOTIFY, CCCD 0x2902) |
| Enquadramento | **1 notificação = 1 frame de 8 bytes.** `BleService::updateFrame` notifica o frame inteiro; cabe no MTU padrão (20 bytes de payload). Não há reenquadramento: notificação com tamanho ≠ 8 é `INVALID_LENGTH`. |
| Taxa | A cada ciclo do `loop()` (~10 ms) o firmware envia `CONTROLLER_STATE` e depois `GRIP_STATE`. |

## 2. Frame

| Offset | Campo | Valor |
|---:|---|---|
| 0 | SYNC1 | `0x53` (`S`) |
| 1 | SYNC2 | `0x4E` (`N`) |
| 2 | Versão | `0x01` |
| 3 | Tipo | tabela 3 |
| 4 | Sequência | u8, **contador único compartilhado por todos os tipos** |
| 5 | Payload 0 | |
| 6 | Payload 1 | |
| 7 | CRC-8 | poly `0x07`, init `0x00`, sem reflexão/XOR final (CRC-8/ATM), sobre bytes 0–6 |

Vetor: `53 4E 01 01 00 FF FF 6F` (CONTROLLER_STATE, seq 0, nada pressionado). Check de "123456789" = `0xF4`.

## 3. Tipos

| Código | Tipo | Emitido pelo firmware atual? | Tratamento no Android |
|---|---|---|---|
| `0x01` | CONTROLLER_STATE | sim | payload u16 LE = registrador SNES (§4); renova watchdog |
| `0x02` | HELLO | não | válido, ignorado (contado) |
| `0x03` | PING | não | válido, ignorado |
| `0x04` | CONFIG | não | válido, ignorado |
| `0x05` | ERROR | não | válido, ignorado |
| `0x06` | GRIP_STATE | sim | payload0: 0 neutro, 1 abrir, 2 fechar; >2 = payload inválido (estado preservado); renova watchdog |
| `0x07` | ACCEL_STATE | não | válido, ignorado (o mapeamento do acelerômetro é feito no firmware) |
| `0x08` | TRANSPORT_SWITCH | não (definido) | válido, ignorado |
| `0x09` | TRANSPORT_ACK | não (definido) | válido, ignorado |
| `0x0A` | ACCEL_CALIBRATED | sim (ao apertar START) | payload0 = 1; payload1 = maior variação ADC entre amostras (máx. 255); registrado no log, não altera estado |
| outros | — | — | `UNKNOWN_TYPE` |

Tipos ignorados **não** renovam o watchdog.

## 4. Registrador de botões (CONTROLLER_STATE)

16 bits, little-endian (`p0 | p1 << 8`), **ativo em LOW** (0 = pressionado). Neutro = `0xFFFF`.

| Bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12–15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Botão | B | Y | SELECT | START | UP | DOWN | LEFT | RIGHT | A | X | L | R | não usados (forçados a 1) |

Modo acelerômetro (SELECT longo, 5 s, no firmware): inclinar para cima → X, baixo → B, direita → A, esquerda → Y. Chega ao Android já como bits deste registrador.

## 5. Validação (ordem)

1. comprimento = 8 → `INVALID_LENGTH`
2. SYNC → `INVALID_SYNC`
3. versão = 1 → `UNSUPPORTED_VERSION`
4. tipo conhecido → `UNKNOWN_TYPE`
5. CRC → `CRC_MISMATCH`
6. sequência → `SEQUENCE_ERROR`

Frames rejeitados não alteram estado nem sequência. Cada motivo tem contador próprio (`dumpsys`) e log com os bytes (limitado a 1/s por motivo).

### Política de sequência

`delta = (seq − último_aceito) mod 256`:
- primeiro frame após conectar: aceito, define a referência;
- `delta = 0` (duplicado) ou `delta > 128` (atrasado): `SEQUENCE_ERROR`;
- `1 ≤ delta ≤ 128`: aceito; `delta − 1` somado em "lost(seq gaps)";
- 4 rejeições seguidas: a referência é redefinida (emissor reiniciou o contador sem desconectar).
