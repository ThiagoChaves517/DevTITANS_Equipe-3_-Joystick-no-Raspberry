# Decisões arquiteturais e estado das pendências

Data de referência: 2026-10-21. Plataforma auditada: GloDroid `gd_rpi4`, AOSP Android 15 de desenvolvimento (`VanillaIceCream`, API 35; `ro.build.version.release` informa 14 por ser build por codinome), kernel 6.6.58, arm64, SELinux **Permissive**.

## Pendências da seção 13

| Decisão | Estado | Resolução / evidência |
|---|---|---|
| Protocolo real do firmware | **Confirmado** | Código do firmware lido; ver `protocol_spec.md`. Sequência compartilhada entre tipos e tipos 0x08/0x09 eram desconhecidos da especificação. |
| UUIDs BLE | **Confirmado** | Iguais em `BleService.cpp` e no app. |
| Enquadramento GATT | **Confirmado (hardware)** | 2570 notificações capturadas, todas com 8 bytes; vetores reais em `FrameDecoderTest.realFirmwareCapture`. |
| Contrato/caminho IPC | **Definido** | `ipc_spec.md`, abstract socket `@snesbt_input`. |
| Suporte/permissões uinput | **Confirmado** | `/dev/uinput` `uhid:uhid 0660`; usuário `shell` está no grupo `uhid`. Dispositivo criado e eventos vistos em `getevent`. |
| Mapeamento botões/acelerômetro | **Definido** | `ipc_spec.md §4`; acelerômetro mapeado no firmware. Grip → `BTN_0/BTN_1` (`BUTTON_1/2`) é escolha nossa: confirmar com a equipe do braço robótico. |
| Timeout do watchdog | **Definido: 100 ms (app) / 500 ms (daemon)** | Medido: CONTROLLER_STATE a 125 Hz, intervalo p50 8 ms, p99 15 ms, máx 23 ms (ADR-007). |
| Inicialização do daemon | **Dev: ADB. Produção: pendente** | `scripts/deploy.sh`. Produção exige serviço `init` (`.rc`) na imagem GloDroid. |
| Execução do serviço Android 14 | **Definido** | Foreground service `connectedDevice`, iniciado por `am start-foreground-service` ou `BOOT_COMPLETED`. |

## ADRs

**ADR-001 — Java no receiver.** O projeto existente já era Java; sem dependências novas. Camadas `protocol` e `core` são Java puro, testadas com JUnit na JVM.

**ADR-002 — Daemon em C++17 com NDK.** Binário único arm64 (`daemon/build.sh`). A saída fica atrás da interface `InputSink`, então mapeamento e sessão IPC são testados no host (`daemon/test.sh`).

**ADR-003 — BLE GATT como transporte inicial.** UART existe no firmware, mas fica fora desta entrega (não objetivo da especificação).

**ADR-004 — IPC por socket abstract + SO_PEERCRED.** Evita permissões de arquivo em `/data/local/tmp` e socket órfão; autoriza por uid. Mensagens de 12 bytes com CRC para detectar dessincronização do stream. Estado completo em cada mensagem.

**ADR-005 — uinput como saída.** Confirmado na plataforma. O JNI antigo (`app/src/main/cpp`) foi removido: o processo do app não tem acesso a `/dev/uinput`.

**ADR-006 — Uma thread de coordenação no app.** BLE, decoder, core e watchdog rodam serializados na `HandlerThread` "snes-core"; o callback do Binder só copia os bytes e posta. O IPC tem thread própria com fila limitada (64 estados, descarta o mais antigo só se saturar). Uma caixa de correio de um único estado foi a primeira versão, mas o teste no dispositivo mostrou toques curtos perdidos quando dois frames chegam em rajada (518 de 522 transições). Nada bloqueia o callback BLE.

**ADR-007 — Valores de watchdog.** O firmware notifica a cada ~10 ms; com prioridade de conexão HIGH o intervalo BLE fica em 7,5–15 ms. O valor de 40 ms citado anteriormente fica a ~3 intervalos e dispararia com qualquer retransmissão. Adotado 100 ms no app (configurável via `--el watchdog_ms N`) e 500 ms no daemon (cobre um app travado; queda de processo já é detectada por EOF). Medição em 2026-10-21 (10 s, link real): p50 8 ms, p99 15 ms, máx 23 ms; 100 ms dá margem > 4× sobre o pior intervalo.

**ADR-008 — Serviço exportado protegido por `android.permission.DUMP`.** Permite controle headless via `am` (shell tem DUMP) sem expor o serviço a outros apps.

## Problemas encontrados na auditoria (app anterior)

- IPC bloqueante (`connect()` com stack trace) dentro do callback BLE a cada frame.
- `GRIP_STATE` (enviado a cada ciclo) era registrado como "frame inválido" genérico.
- Sem watchdog no app; sem máquina de estados; lógica inteira na Activity.

## Teste de integração no Raspberry (sem ESP32)

`scripts/device_test.py` injeta frames v1 reais no serviço (ação `INJECT`, só em build debug) pelo mesmo caminho das notificações BLE e verifica kernel (`getevent`), Android (`dumpsys input`) e contadores. Resultado em 2026-10-21: **50/50 em 3 rodadas seguidas**. Cobre os 12 botões, combinações, diagonal, garra, os 6 motivos de rejeição, tipos ignorados, watchdog (neutralização em 100 ms, uma única vez), 400 frames aleatórios a 10 ms (transições exatas), queda/retorno do daemon com botão segurado, morte do app com botão segurado e latência no app (média ~2–4 ms entre a notificação e a escrita no socket).

Correções que o teste revelou:
- Garra: `BTN_TRIGGER_HAPPY1/2` chegava ao Android como `keyCode=UNKNOWN` (o `Generic.kl` não mapeia). Trocado para `BTN_0/BTN_1` → `BUTTON_1/2`.
- IPC: caixa de correio de um estado perdia toques curtos (ver ADR-006).

Não coberto por injeção: rádio BLE (scan, GATT, desconexão real), que depende do ESP32.

## Firmware: problema encontrado e corrigido

Após a desconexão do app antigo (12:51:05 no relógio do Raspberry), o ESP32 `24:0A:C4:9B:D0:FE` deixou de anunciar: um scan sem filtro viu ~20 dispositivos BLE, mas não o ESP32. Causa identificada no firmware oficial (`../firmware`, versão local): `BleService.cpp` chama `server->startAdvertising()` dentro de `onDisconnect`. No core 3.3.7 com Bluedroid esse callback roda na task BTC, e `BLEAdvertising::start()` espera com `portMAX_DELAY` um evento GAP que só essa mesma task entrega: a pilha BLE trava após a primeira desconexão, enquanto o `loop()` segue imprimindo `[STATUS]`. Sintoma esperado no Serial: `[BLE] Cliente desconectado.` sem `[BLE] Advertising reiniciado.`. Correção aplicada em `../firmware` e gravada em 2026-10-21: o callback só sinaliza e `BleService::poll()` reinicia o advertising no `loop()` após 500 ms. Validação: 5/5 desconexões `CONNECTION_TERMINATED_BY_LOCAL_HOST (0x16)` seguidas de reconexão em 1,3–1,7 s.

Também corrigido: X/Y (GPIO4/5) em `INPUT` sem pull-up. No hardware, o firmware antigo enviava X pressionado permanentemente (`0xFDFF`); com `INPUT_PULLUP` o controle fica neutro (30 s em repouso: 7592 notificações válidas, 0 mudanças de estado). `connected` passou a `volatile` e o `[STATUS]` mostra `BLE=CONECTADO/SEM_CLIENTE`. Pendente de teste humano: pressionar cada botão físico e o modo acelerômetro. A pinagem válida é a do `BoardConfig.h`; os READMEs do firmware descrevem uma pinagem anterior.
