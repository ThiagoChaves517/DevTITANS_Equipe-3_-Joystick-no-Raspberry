# Operação e diagnóstico

## Implantar (desenvolvimento)

```sh
scripts/deploy.sh                 # compila app + daemon, instala, concede permissões, inicia tudo
WATCHDOG_MS=150 scripts/deploy.sh # watchdog do app diferente do padrão (100 ms)
scripts/deploy.sh stop
```

## Testes sem hardware

```sh
./gradlew :app:testDebugUnitTest   # protocolo, core, watchdog, IPC (Java)
daemon/test.sh                     # IPC, mapeamento, sessão (C++ no host)
```

## Teste de integração no Raspberry (sem ESP32)

```sh
scripts/deploy.sh && python3 scripts/device_test.py
```

Injeta frames reais no serviço (só build debug) e confere eventos no kernel, no Android e os contadores. Injeção manual:

```sh
adb shell am start-foreground-service -n com.snesbt.receiver/.service.ControllerService \
    -a com.snesbt.receiver.INJECT --es frames 534e010164fffe06 --ei interval_ms 10
```

## Controle manual

```sh
adb shell am start-foreground-service -n com.snesbt.receiver/.service.ControllerService [--el watchdog_ms 100]
adb shell am stopservice -n com.snesbt.receiver/.service.ControllerService
adb shell "nohup /data/local/tmp/snes_input_daemon --allow-uid <uid> > /data/local/tmp/snes_input_daemon.log 2>&1 &"
```

## Diagnóstico

```sh
adb shell dumpsys activity service com.snesbt.receiver/.service.ControllerService   # estado, contadores, latência
adb logcat -s SnesBtReceiver SnesBle SnesDaemonClient SnesDaemon
adb shell setprop log.tag.SnesBleFrames VERBOSE      # loga cada notificação (len + bytes)
adb shell getevent -lt                                # eventos do gamepad virtual
adb shell /data/local/tmp/ipc_probe FEFF FFFF         # testa o daemon sem o app (pressiona/solta A)
adb shell dumpsys input | grep -A3 SNES-BT            # classificação no Android
```

Estados do serviço: `STOPPED → STARTING → DAEMON_CONNECTING → BLE_SCANNING → BLE_CONNECTING → DISCOVERING_SERVICES → READY`; `RECOVERING` após perder o daemon ou o BLE; `ERROR` exige intervenção (ex.: permissões).

## Pendente para produção (Etapa 6)

- Serviço `init` para o daemon na imagem GloDroid (usuário com grupo `uhid`, sem root).
- Política SELinux para `untrusted_app` conectar ao socket do daemon (a imagem atual é Permissive).
- Teste de reboot completo.
