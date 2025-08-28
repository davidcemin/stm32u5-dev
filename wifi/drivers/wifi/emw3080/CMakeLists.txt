# Copyright (c) 2025 David Cemin
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

# Use standard log levels defined in Zephyr
# No need for special WIFI_LOG_LEVEL definitions

zephyr_library_sources(
    emw3080.c
    emw3080_offload.c
    emw3080_debug.c
    emw3080_shell.c
    emw3080_fallback.c
)
