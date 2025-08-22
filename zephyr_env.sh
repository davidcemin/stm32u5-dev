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
# Dynamically detect the latest installed Zephyr SDK version
ZEPHYR_SDK_PARENT="$HOME/proj/zephyr/zephyr-sdk"
ZEPHYR_SDK_INSTALL_DIR="$(ls -d "${ZEPHYR_SDK_PARENT}"/zephyr-sdk-* 2>/dev/null | sort -V | tail -n 1)"
if [ -z "$ZEPHYR_SDK_INSTALL_DIR" ]; then
    echo "❌ No Zephyr SDK installation found in $ZEPHYR_SDK_PARENT" >&2
    return 1 2>/dev/null || exit 1
fi
export ZEPHYR_SDK_INSTALL_DIR
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
echo "   GDB:            $(command -v arm-zephyr-eabi-gdb || echo 'not found')"
echo "   Toolchain:      $(command -v arm-zephyr-eabi-gcc || echo 'not found')"
echo "   GDB:            $(command -v arm-zephyr-eabi-gdb || echo 'not found')"
echo "   OpenOCD:        $(command -v openocd || echo 'not found')"

