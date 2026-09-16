#!/bin/sh
# Fetch a standalone CMSIS header set for `make arm` in a bare checkout or
# CI, laid out the way the CubeIDE project's Drivers/CMSIS is, so the same
# CMSIS_DIR logic works either way.
set -e
cd "$(dirname "$0")/.."
DEV=https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-f4/master/Include
CORE=https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include
OUT=.cmsis
mkdir -p "$OUT/Device/ST/STM32F4xx/Include" "$OUT/Include"
for f in stm32f407xx.h stm32f4xx.h system_stm32f4xx.h; do
    curl -fsSL "$DEV/$f" -o "$OUT/Device/ST/STM32F4xx/Include/$f"
done
for f in core_cm4.h cmsis_gcc.h cmsis_compiler.h cmsis_version.h mpu_armv7.h; do
    curl -fsSL "$CORE/$f" -o "$OUT/Include/$f"
done
echo "CMSIS headers placed in $(pwd)/$OUT"
echo "Build with: make arm CMSIS_DIR=$OUT"
