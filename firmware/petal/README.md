# Petal firmware v3 ("light touch")

ESP32-C3 SuperMini, one per petal. Same wiring, I2C address (0x10 + petal ID) and
hub packet as v2, so the hub and app work unchanged.

What changed from v2 (keycaps rest on the FSRs, so presses move the sensor less):
- 8x oversampling + a light low-pass filter per key: lower noise floor
- threshold set by a **sensitivity level 1–9** (default 6; v2 was about 3)
- a press must stay above threshold for 3 ms, which rejects one-sample spikes
- slow baseline tracking at rest, so keycap settling can't become phantom notes
- lighter presses reach higher velocities (FULL_MIN 0.20 → 0.10)

## Build (Mac)

The Arduino ctags tool is an Intel binary and fails on Apple Silicon without
Rosetta. Every function here is defined before it's used, so ctags isn't needed:
point it at a no-op stub.

```
mkdir -p ~/tunagon_build/ctags_shim
printf '#!/bin/sh\nexit 0\n' > ~/tunagon_build/ctags_shim/ctags && chmod +x ~/tunagon_build/ctags_shim/ctags
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc \
  --build-path ~/tunagon_build/v3/o01 \
  --build-property runtime.tools.ctags.path=$HOME/tunagon_build/ctags_shim \
  --build-property "compiler.cpp.extra_flags=-MMD -c -DPETAL_ID=1" .
```
Flash `petal.ino.merged.bin` at address `0x0` (esptool-js, 921600).
Built images for petals 1–12: `~/Desktop/Tunagon/Firmware/Petal/6-key v3 light touch/`.

## Tuning sensitivity without reflashing

Open the petal's USB serial port at 115200 (esptool-js Console). It prints
`raw:` lines: each key's signal above rest / its threshold (`*` = on).
Type `s8` + Enter for sensitivity 8 (1 = least, 9 = most); it's saved on the
board. `s?` prints the current level. If you get phantom notes, go down one level.
