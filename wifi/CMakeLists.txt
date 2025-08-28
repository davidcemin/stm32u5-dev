# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Set configurations before finding Zephyr
set(CONF_FILE "prj.conf")

# Find Zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(emw3080_wifi_sample)

# Include our custom drivers
target_include_directories(app PRIVATE drivers)
# Include paths need to be directories, not files
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/include/zephyr/net)
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/include/zephyr/drivers)

# Add driver sources directly to the application
target_sources(app PRIVATE 
    src/main.c 
    src/wifi_shell.c
    drivers/wifi/emw3080/emw3080.c
    drivers/wifi/emw3080/emw3080_offload.c
    drivers/wifi/emw3080/emw3080_debug.c
    drivers/wifi/emw3080/emw3080_fallback.c
    drivers/wifi/emw3080/emw3080_net.c
)

# Ensure we use networking configuration
target_compile_definitions(app PRIVATE
    -DCONFIG_NETWORKING=1
    -DCONFIG_NET_NATIVE=1
    -DCONFIG_NET_DRIVERS=1
    -DCONFIG_NET_OFFLOAD=1
    -DCONFIG_NET_SOCKETS_OFFLOAD=1
)
