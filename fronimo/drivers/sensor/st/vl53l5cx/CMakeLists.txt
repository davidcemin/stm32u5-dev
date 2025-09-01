# ST Microelectronics VL53L5CX Time-of-Flight sensor

# Copyright (c) 2025 David Cemin
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_VL53L5CX vl53l5cx.c)
zephyr_library_sources_ifdef(CONFIG_VL53L5CX_TRIGGER vl53l5cx_trigger.c)
