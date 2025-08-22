#!/usr/bin/env bash
#
# Zephyr environment setup script (STM32 / ST boards on macOS)
# Keeps toolchains and OpenOCD clean, no Espressif conflicts.

# ------------------------------------------------------------------
# Zephyr project base
# ------------------------------------------------------------------
export ZEPHYR_BASE="$HOME/proj/zephyr/zephyr-env/workspace/zephyr"

# ------------------------------------------------------------------
# Zephyr SDK
# ------------------------------------------------------------------
export ZEPHYR_SDK_INSTALL_DIR="$HOME/proj/zephyr/zephyr-sdk/zephyr-sdk-0.17.4"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

# ------------------------------------------------------------------
# Toolchain paths (ARM only, since this env is for STM32 boards)
# ------------------------------------------------------------------
export PATH="${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin:$PATH"

# ------------------------------------------------------------------
# OpenOCD setup
# ------------------------------------------------------------------
# On Linux: Zephyr SDK bundles its own OpenOCD
# On macOS: use Homebrew OpenOCD
if [ -d "${ZEPHYR_SDK_INSTALL_DIR}/openocd/bin" ]; then
    export PATH="${ZEPHYR_SDK_INSTALL_DIR}/openocd/bin:$PATH"
elif command -v brew &>/dev/null && [ -x "/opt/homebrew/bin/openocd" ]; then
    export PATH="/opt/homebrew/bin:$PATH"
fi

# ------------------------------------------------------------------
# CMake package registry setup (handled by setup.sh, but ensure here)
# ------------------------------------------------------------------
export CMAKE_PREFIX_PATH="${ZEPHYR_SDK_INSTALL_DIR}:${CMAKE_PREFIX_PATH}"

# ------------------------------------------------------------------
# Helper output
# ------------------------------------------------------------------
echo "✅ STM32 Zephyr environment configured"
echo "   Zephyr base:    $ZEPHYR_BASE"
echo "   SDK:            $ZEPHYR_SDK_INSTALL_DIR"
echo "   Toolchain:      $(which arm-zephyr-eabi-gcc)"
echo "   GDB:            $(which arm-zephyr-eabi-gdb)"
echo "   OpenOCD:        $(which openocd)"

