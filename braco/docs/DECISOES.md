# Decisões: integração Raspberry → braço (`snes_arm_daemon`)

Data de referência: 2026-11-05. Mesma plataforma do driver: GloDroid `gd_rpi4`, Android 15 (API 35),
kernel 6.6.58, arm64, SELinux **Permissive**. Substitui a primeira versão de 2026-10-21, que tinha uma bridge
com protocolo de ângulos `J`, páginas com START e modo GPIO direto. Ela continua no histórico do git.

## Requisitos (Equipe Robô, 2026-11-05)

- Um **segundo driver** que lê o evento disparado pelo driver do Raphael e o manda para a serial da Raspberry.
- A Raspberry liga na serial do ESP32 e **repassa**, **sem resposta do braço**.
- O que viaja são os **16 bits que o controle envia**.
- A página "Braço Robótico ‐ Semana 09\09‐21\10" **não muda**: mapeamento, pinos e ESP32 via serial USB.

## ADRs

**ADR-001: Ler a camada de eventos de input, não o IPC.** O `snes_input_daemon` publica o controle como o
gamepad `SNES-BT-Controller` (uinput). O `snes_arm_daemon` lê esse dispositivo por evdev, sem `EVIOCGRAB`, e o
Android continua recebendo o controle. Não há acoplamento com o socket `@snesbt_input` nem com o app. Se um
dia o joystick vier por cabo ou por um módulo de kernel, nada muda aqui, desde que o gamepad exista.

**ADR-002: C++17 com NDK, no mesmo molde do `snes_input_daemon`.**
- Binário arm64 em `/data/local/tmp` (`build.sh`), com `-Wall -Wextra -Werror` e `-static-libstdc++`.
- `log.h` igual ao dele (stderr + logcat, tag `SnesArmDaemon`).
- A saída fica atrás de uma interface (`FrameSink`), como o `InputSink` dele. Assim o mapeamento e a política
  de envio são testados no host (`test.sh`).
- Namespace próprio (`snes_arm`) para compilar junto com o código dele nos testes.

**ADR-003: Na serial vai o quadro v1 de 8 bytes do joystick, não 2 bytes crus.** Foi escolha da equipe. O
payload continua sendo os 16 bits. Com o SYNC e o CRC, o ESP32 se ressincroniza sozinho se um byte se perder.
Com 2 bytes crus, ele passaria a ler os pares trocados sem perceber. O ESP32 do braço usa o mesmo
`FrameProtocol` do joystick.

**ADR-004: Estado completo, mudança + keepalive de 100 ms, sem tráfego em repouso.** É a política do app do
Raphael (`ipc_spec.md §3`). Como o braço não confirma nada, a segurança depende de o ESP32 parar sozinho: o
watchdog de 250 ms do fluxograma do William, que tolera 1–2 quadros perdidos com o keepalive de 100 ms.

**ADR-005: Mapeamento no ESP32, numa tabela única (`ArmMapping.h`).** A Raspberry só repassa. A tabela da
wiki do William fica num header C++ puro, compilado pelo firmware, pelos testes do daemon e pelo
`frame_sniffer`; o simulador replica a mesma tabela. Mudar o mapeamento é mudar uma função.

**ADR-006: "Start = parar os movimentos" vale enquanto ele estiver pressionado.** O firmware do joystick
também usa o START para calibrar o acelerômetro. Com a leitura literal, sem trava liga/desliga, apertar START
para calibrar só para o braço naquele instante, e soltar devolve o controle. Não existe um estado escondido
que deixe o braço "travado".

**ADR-007: A porta do braço é identificada pelo sysfs, sem abrir as outras.** O diagrama do William põe os
dois ESP32 na USB (`ttyUSB0`/`ttyUSB1`, "a ordem pode variar"). O braço não responde, então não dá para
perguntar quem ele é, e abrir a porta do joystick pulsaria DTR/RTS e **reiniciaria o ESP32 do joystick**.
- `--device auto` só serve quando há uma única serial USB. Com mais de uma, o daemon recusa e lista as
  candidatas.
- `--device usb:1-1.3` fixa a **porta física**, que não muda quando a enumeração troca de ordem.
- `--list` mostra o que a placa vê.

**ADR-008: Esperar o boot do ESP32 ao abrir a serial (1500 ms).** No ESP32 DevKit, abrir a serial costuma
reiniciar a placa. Um quadro mandado durante o boot se perde. Por isso o primeiro envio espera
`--boot-wait-ms`, e o keepalive cobre o resto.

**ADR-009: A garra do acelerômetro (`BTN_0`/`BTN_1`) não é repassada.** Os 16 bits não têm lugar para ela, e
na tabela do William a garra é A/X. Se a equipe quiser a garra por inclinação, o caminho é mandar também o
quadro `GRIP_STATE` (tipo 0x06), que o decodificador do ESP32 já aceita como válido.

**ADR-010: O firmware usa os nomes e os blocos do fluxograma do William.** Ficam as variáveis `angBase…`,
`dirBase…`, `MIN_xxx`/`MAX_xxx`, `ultimoMovimento`, `INTERVALO_MS`, e as funções `lerSerial()` →
`interpretaEstado()` → `atualizaAngulo()`, na ordem do fluxograma. Quem conhece o desenho reconhece o código
sem precisar de tradução. Só a leitura muda: entra o quadro com os 16 bits no lugar da linha de texto. A
checagem item a item está em [CONFORMIDADE_WILLIAM.md](CONFORMIDADE_WILLIAM.md).

## Divergências encontradas na documentação do time

| Onde | O que diz | O que foi seguido |
|---|---|---|
| Fluxograma em `resources/` (pinos) | base 13, ombro 14, cotovelo 25, garra 26 | **Wiki e diagrama de ligações: 25/26/27/12**. Os dois concordam e a página "não muda" |
| Fluxograma (entrada) | "lê linha da Serial e interpreta 4 valores direcionais" | **16 bits do controle** no quadro v1, como pedido. O resto do fluxograma (25 ms, 1°, limites, 250 ms) foi seguido |
| Comentário no `InputManager.cpp` do joystick | "A/B (garra), X/Y (frente/trás)" | **Wiki do William: A/X garra, B/Y cotovelo** |
| GPIO12 (garra) | pino de *strapping* do ESP32 | Mantido, porque a página não muda. Aviso no `ArmConfig.h` e no README: se a placa não ligar com a garra conectada, solte o fio no boot |

## Pendências

| Item | Estado |
|---|---|
| Teste no Raspberry com o daemon real do Raphael (`scripts/device_test.py`) | Script pronto. Aguarda a placa conectada |
| Firmware num ESP32 real | Compilado e executado no computador (`host_test`). Falta gravar e testar com servos |
| Calibração dos limites (`ArmConfig.h`) | Valores de exemplo do fluxograma |
| Início automático no boot | Opcional (`aosp/`), exige rebuild do AOSP |
| Política SELinux própria | Imagem atual Permissive. Em Enforcing, o daemon precisaria de domínio próprio |
