# Opcional: integrar a bridge no AOSP (GloDroid)

**Não é necessário para testar:** tudo funciona rodando de `/data/local/tmp` com `adb root`.
Use isto quando quiserem que a bridge inicie sozinha no boot. Exige rebuild do AOSP, que roda no
computador de build da equipe.

## 1. Binário + serviço de init

1. Copie `braco/` para dentro da árvore, por exemplo `aosptree/vendor/devtitans/braco/`.
   O `braco/Android.bp` define `snes_arm_bridge` e `snes_fake_pad`, e o serviço `aosp/snes_arm_bridge.rc`.
2. Adicione ao `device.mk` do `gd_rpi4`:
   ```make
   PRODUCT_PACKAGES += snes_arm_bridge snes_fake_pad
   ```
3. Rebuild e grave a imagem (wiki "Raspberry ‐ Semana 31\08 ‐ 08\09" e "Raspberry ‐ Semanas 09\09 ‐ 21\09").

O build atual tem SELinux **permissive**, então o serviço roda sem política própria. Para um
build `enforcing` seria preciso um domínio SELinux, que não está incluído aqui.

## 2. (Se o ESP32 do braço tiver CH340) driver no kernel

O kernel `6.6.58-android15-0-GloDroid` tem `CONFIG_USB_SERIAL_CP210X=y` e `CONFIG_USB_ACM=y`,
mas **não** tem `CONFIG_USB_SERIAL_CH341`. Para placas CH340, adicione ao fragmento de
configuração do kernel do GloDroid:

```
CONFIG_USB_SERIAL_CH341=y
```

## 3. (Se forem usar a UART dos pinos 14/15) console

O `ttyS0` (GPIO14/15) é `console=ttyS0,115200` no cmdline e tem o serviço `console` (um `sh`)
rodando nele. Para usar essa UART com um ESP32, tire o `console=` do cmdline do bootloader e
desabilite o serviço `console` no init. Em teste também dá para fazer
`adb shell setprop ctl.stop console`, que vale até reiniciar.
