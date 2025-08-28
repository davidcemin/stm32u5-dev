# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Simple configuration that doesn't try to add a custom driver
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(emw3080_wifi_sample)

# Include our custom drivers
target_include_directories(app PRIVATE drivers)

# Add driver sources directly to the application
target_sources(app PRIVATE 
    src/main.c 
    src/wifi_shell.c
    drivers/wifi/emw3080/emw3080.c
    drivers/wifi/emw3080/emw3080_offload.c
    drivers/wifi/emw3080/emw3080_debug.c
    drivers/wifi/emw3080/emw3080_fallback.c
)
