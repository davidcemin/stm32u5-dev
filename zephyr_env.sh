#!/usr/bin/env bash
# =============================================================================
# Zephyr + ESP-IDF environment setup (doctor + board qualifiers)
# =============================================================================

# ---- Directories ----
export ZEPHYR_WORKSPACE="$HOME/proj/zephyr/zephyr-env/workspace"
export ZEPHYR_BASE="$ZEPHYR_WORKSPACE/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk"

# Your ESP-IDF checkout
: "${IDF_PATH:=$HOME/proj/esp/esp-idf}"

# ---- Python venv (auto-activate if present) ----
VENV_PATH="$HOME/proj/zephyr/zephyr-env/.zephyr-venv"
if [ -d "$VENV_PATH" ]; then
  # shellcheck disable=SC1090
  source "$VENV_PATH/bin/activate"
  export ZEPHYR_PYTHON_ENV=1
fi

# ---- Paths ----
export PATH="$HOME/.local/bin:$PATH"
export PATH="/opt/ST/STM32CubeCLT_1.19.0/STM32CubeProgrammer/bin:$PATH"
# IDF export normally sets these; leaving here as helpful extras:
# Robustly add xtensa-esp-elf toolchain bin to PATH (handles multiple/zero matches)
XTENSA_ESP_ELF_BIN_DIRS=$(find "$HOME/.espressif/tools/xtensa-esp-elf" -type d -name 'xtensa-esp-elf' -prune -exec find {} -type d -name bin \; 2>/dev/null)
if [ -n "$XTENSA_ESP_ELF_BIN_DIRS" ]; then
  # Add all found bin dirs to PATH
  while IFS= read -r dir; do
    export PATH="$dir:$PATH"
  done <<< "$XTENSA_ESP_ELF_BIN_DIRS"
fi

# ---- ESP-IDF export (auto) ----
if [ -d "$IDF_PATH" ] && [ -f "$IDF_PATH/export.sh" ]; then
  # shellcheck disable=SC1090
  . "$IDF_PATH/export.sh" >/dev/null 2>&1 && export ESP_IDF_SOURCED=1
  if command -v xtensa-esp32s3-elf-gcc >/dev/null 2>&1; then
    export ESPRESSIF_TOOLCHAIN_PATH="$(dirname "$(dirname "$(command -v xtensa-esp32s3-elf-gcc)")")"
  fi
fi

# ---- small utils ----
_need() { command -v "$1" >/dev/null 2>&1; }

# ---- workspace ----
zwork() {
  if [ ! -d "$ZEPHYR_BASE" ]; then
    echo "❌ Zephyr not found at $ZEPHYR_BASE (run west init/update first?)"
    return 1
  fi
  (cd "$ZEPHYR_WORKSPACE" && west zephyr-export >/dev/null 2>&1) || true
  cd "$ZEPHYR_WORKSPACE" || return
  echo "📂 Now in Zephyr workspace: $ZEPHYR_WORKSPACE"
}

# ---- board qualifier expansion ----
__qualify_board() {
  # Expand short Zephyr board names to full targets (edit/extend as needed)
  case "$1" in
    # ESP32 family
    esp32s3_devkitm) echo "esp32s3_devkitm/esp32s3/procpu" ;;
    esp32s3_devkitc) echo "esp32s3_devkitc/esp32s3/procpu" ;;
    esp32_devkitc)   echo "esp32_devkitc/esp32/procpu" ;;
    esp32c3_devkitm) echo "esp32c3_devkitm/esp32c3" ;;    # single-core
    esp32s2_saola)   echo "esp32s2_saola/esp32s2" ;;       # single-core
    # ST board you mentioned earlier works as-is:
    b_u585i_iot02a)  echo "b_u585i_iot02a" ;;
    # default (unchanged)
    *) echo "$1" ;;
  esac
}

# ---- toolchain variant chooser ----
__variant_for_board() {
  case "$1" in
    esp32*|esp32s*|esp32c*|esp32h*) echo "espressif" ;;
    native*|posix*)                 echo "host" ;;
    *)                              echo "zephyr" ;;
  esac
}

# ---- build / flash / term ----
zbuild() {
  if [ $# -lt 2 ]; then
    echo "Usage: zbuild <BOARD> <APP_PATH> [-- extra cmake args]"
    return 2
  fi
  _need west || { echo "❌ west missing (pip install west)"; return 1; }

  local BOARD_RAW="$1"; shift
  local APP="$1"; shift
  local BOARD="$(__qualify_board "$BOARD_RAW")"
  local VARIANT="$(__variant_for_board "$BOARD_RAW")"

  zwork || return $?

  # Espressif extras
  if [ "$VARIANT" = "espressif" ]; then
    if [ -z "$ESP_IDF_SOURCED" ]; then
      echo "⚠️ ESP-IDF not sourced (IDF_PATH=$IDF_PATH). Run:  . \"$IDF_PATH/export.sh\""
    fi
    if ! command -v xtensa-esp32s3-elf-gcc >/dev/null 2>&1; then
      echo "❌ Xtensa S3 toolchain not found on PATH. Install via IDF: ./install.sh esp32s3 ; . ./export.sh"
      return 1
    fi
    [ -n "$ESPRESSIF_TOOLCHAIN_PATH" ] || echo "⚠️ ESPRESSIF_TOOLCHAIN_PATH not set (compiler is on PATH, so build should still work)"
  fi

  local GEN="-GNinja"
  _need ninja || { echo "⚠️ Ninja not found; using Unix Makefiles"; GEN="-GUnix Makefiles"; }

  ( cd "$ZEPHYR_WORKSPACE" && \
    west build -b "$BOARD" "$APP" --pristine $GEN \
      -DZEPHYR_TOOLCHAIN_VARIANT="$VARIANT" "$@" )
}

zflash() { ( cd "$ZEPHYR_WORKSPACE" && west flash "$@" ); }

zterm() {
  local port="${1:-/dev/tty.usbserial-XXXX}"
  local baud="${2:-115200}"
  if _need idf.py; then idf.py -p "$port" monitor --baud "$baud"
  else python -m serial.tools.miniterm "$port" "$baud"
  fi
}

zstatus() {
  echo "================ Zephyr Status ================"
  echo "Workspace : $ZEPHYR_WORKSPACE"
  echo "ZEPHYR_BASE: $ZEPHYR_BASE"
  echo "Zephyr SDK: $ZEPHYR_SDK_INSTALL_DIR"
  echo "ESP-IDF   : $IDF_PATH (sourced: ${ESP_IDF_SOURCED:-no})"
  echo "Toolchain : ${ESPRESSIF_TOOLCHAIN_PATH:-<unset>}"
  echo "Python venv active: ${ZEPHYR_PYTHON_ENV:-no}"
  echo "ninja: $([ "$(_need ninja; echo $?)" = 0 ] && ninja --version || echo 'not found')"
  echo "west : $([ "$(_need west; echo $?)" = 0 ] && west --version || echo 'not found')"
  echo "cmake: $([ "$(_need cmake; echo $?)" = 0 ] && cmake --version | head -n1 || echo 'not found')"
  echo "dtc  : $([ "$(_need dtc; echo $?)" = 0 ] && dtc --version 2>/dev/null | head -n1 || echo 'not found')"
  echo "xtensa-esp32s3-elf-gcc: $([ "$(_need xtensa-esp32s3-elf-gcc; echo $?)" = 0 ] && xtensa-esp32s3-elf-gcc --version | head -n1 || echo 'not found')"
  echo "openocd: $([ "$(_need openocd; echo $?)" = 0 ] && openocd --version | head -n1 || echo 'not found')"
  echo "idf.py : $([ "$(_need idf.py; echo $?)" = 0 ] && idf.py --version || echo 'not found')"
  echo "================================================"
}

zdoctor() {
  local ok=1
  echo "🔎 Running post-sourcing checks…"
  [ -d "$ZEPHYR_BASE" ] && echo "✅ Zephyr base: $ZEPHYR_BASE" || { echo "❌ Missing $ZEPHYR_BASE"; ok=0; }
  _need west  && echo "✅ west present"  || { echo "❌ west missing (pip install west)"; ok=0; }
  _need cmake && echo "✅ cmake present" || { echo "❌ cmake missing (brew install cmake)"; ok=0; }
  _need ninja && echo "✅ ninja present" || echo "⚠️ ninja missing (brew install ninja) — falling back to Makefiles"
  if [ -n "$ESP_IDF_SOURCED" ]; then echo "✅ ESP-IDF sourced ($IDF_PATH)"; else echo "⚠️ ESP-IDF not auto-sourced"; fi
  _need xtensa-esp32s3-elf-gcc && echo "✅ Xtensa S3 toolchain found" || { echo "❌ Xtensa S3 toolchain not found (use IDF install/export)"; ok=0; }
  _need openocd && echo "✅ openocd present" || echo "⚠️ openocd not found"
  _need dtc && : || echo "⚠️ dtc not found (brew install dtc)"
  [ "$ok" -eq 1 ] && echo "✅ Environment looks good." || echo "❌ Some required pieces are missing."
}

# ---- greet + doctor ----
echo "✅ Zephyr + ESP-IDF + Python environment ready."
echo "   Run 'zwork' to jump into workspace, 'zstatus' to check status."
zdoctor

