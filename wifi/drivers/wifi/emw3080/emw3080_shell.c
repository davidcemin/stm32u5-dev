/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include "emw3080_debug.h"

static int cmd_emw_debug(const struct shell *sh, size_t argc, char *argv[])
{
    shell_print(sh, "Starting EMW3080 driver debug...");
    
    /* Call our debug functions */
    emw3080_debug_list_devices();
    emw3080_debug_list_interfaces();
    emw3080_debug_check_initialization();
    
    return 0;
}

/* Check status of the uart4 device */
static int cmd_emw_uart(const struct shell *sh, size_t argc, char *argv[])
{
    const struct device *uart4 = device_get_binding("uart4");
    
    if (uart4) {
        shell_print(sh, "UART4 device exists: %s", uart4->name);
        shell_print(sh, "UART4 is %s", device_is_ready(uart4) ? "ready" : "not ready");
    } else {
        shell_print(sh, "UART4 device not found in system!");
    }
    
    return 0;
}

/* Dump the device tree node for uart4 */
static int cmd_emw_dt(const struct shell *sh, size_t argc, char *argv[])
{
    shell_print(sh, "Device tree node check for EMW3080:");
    
    if (DT_NODE_HAS_STATUS(DT_NODELABEL(uart4), okay)) {
        shell_print(sh, "UART4 node is enabled in device tree");
        
        /* Check for EMW3080 child node */
        if (DT_NODE_HAS_COMPAT(DT_CHILD(DT_NODELABEL(uart4), emw3080), mxchip_emw3080)) {
            shell_print(sh, "Found EMW3080 child node under UART4");
            shell_print(sh, "EMW3080 node status: %s", 
                  DT_NODE_HAS_STATUS(DT_CHILD(DT_NODELABEL(uart4), emw3080), okay) ? "okay" : "disabled");
        } else {
            shell_print(sh, "No EMW3080 child node found under UART4");
        }
    } else {
        shell_print(sh, "UART4 node is not enabled in device tree");
    }
    
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(emw_cmds,
    SHELL_CMD(debug, NULL, "Run EMW3080 debug functions", cmd_emw_debug),
    SHELL_CMD(uart, NULL, "Check UART4 status", cmd_emw_uart),
    SHELL_CMD(dt, NULL, "Check device tree nodes", cmd_emw_dt),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(emw, &emw_cmds, "EMW3080 debug commands", NULL);
