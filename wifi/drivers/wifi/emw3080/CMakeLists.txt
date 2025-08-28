# Copyright (c) 2025 David Cemin
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_compile_definitions_ifdef(CONFIG_WIFI_LOG_LEVEL_OFF WIFI_LOG_LEVEL=0)
zephyr_library_compile_definitions_ifdef(CONFIG_WIFI_LOG_LEVEL_ERR WIFI_LOG_LEVEL=1)
zephyr_library_compile_definitions_ifdef(CONFIG_WIFI_LOG_LEVEL_WRN WIFI_LOG_LEVEL=2)
zephyr_library_compile_definitions_ifdef(CONFIG_WIFI_LOG_LEVEL_INF WIFI_LOG_LEVEL=3)
zephyr_library_compile_definitions_ifdef(CONFIG_WIFI_LOG_LEVEL_DBG WIFI_LOG_LEVEL=4)

zephyr_library_sources(
    emw3080.c
    emw3080_offload.c
)
