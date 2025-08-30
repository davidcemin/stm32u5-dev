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
    src/emw3080_network.c
    src/emw3080_init.c  # EMW3080 device initialization
    src/tests/slip_test.c
    src/tests/emw3080_spi_test.c  # SPI test - now needed for main function
    src/tests/emw3080_hci_test.c  # HCI test - for bottom-up testing
    drivers/wifi/emw3080/emw3080.c  # Main EMW3080 device driver
    drivers/wifi/emw3080/emw3080_l2.c  # L2 layer functions
    drivers/wifi/emw3080/emw3080_net.c  # Network device functions
    drivers/wifi/emw3080/emw3080_offload.c  # Network offload functions
    drivers/wifi/emw3080/emw3080_socket.c  # Socket functions
    drivers/wifi/emw3080/emw3080_mipc.c  # MIPC protocol functions
    drivers/wifi/emw3080/emw3080_mipc_spi.c  # MIPC SPI functions
    drivers/wifi/emw3080/emw3080_main_stubs.c
    drivers/wifi/emw3080/emw3080_spi.c
    drivers/wifi/emw3080/emw3080_slip.c
    drivers/wifi/emw3080/emw3080_ipc.c
    drivers/wifi/emw3080/emw3080_hci.c
    drivers/wifi/emw3080/emw3080_mgmt.c
    # drivers/wifi/emw3080/emw3080_compat.c  # Excluded to avoid conflicts with real device
)

# Ensure we use networking configuration
target_compile_definitions(app PRIVATE
    -DCONFIG_NETWORKING=1
    -DCONFIG_NET_NATIVE=1
    -DCONFIG_NET_DRIVERS=1
    -DCONFIG_NET_OFFLOAD=1
    -DCONFIG_NET_SOCKETS_OFFLOAD=1
    -DCONFIG_NET_L2_ETHERNET=1
)
