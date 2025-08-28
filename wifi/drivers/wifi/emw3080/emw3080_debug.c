/*
 * Copyright (#include <zephyr/sys/printk.h>

#include "emw3080.h"
#include "emw3080_socket.h"2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mxchip_emw3080 
/* Note: This must match the compatible string "mxchip,emw3080" in device tree 
 * but with commas replaced by underscores */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_debug, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/printk.h>

#include "emw3080.h"

/* This file adds additional debugging to help diagnose EMW3080 driver issues */

/* Function to list all devices in the system */
void emw3080_debug_list_devices(void)
{
    LOG_INF("=== Listing all devices in system ===");
    
    /* Internal implementation to list devices without relying on shell */
    #ifdef CONFIG_DEVICE_ENUM
    const struct device *dev = NULL;
    
    STRUCT_SECTION_FOREACH(device, dev) {
        if (dev && device_is_ready(dev)) {
            LOG_INF("Device: %s (ready)", dev->name);
        } else if (dev) {
            LOG_INF("Device: %s (not ready)", dev->name);
        }
    }
    #else
    LOG_INF("Device enumeration not enabled (CONFIG_DEVICE_ENUM not set)");
    LOG_INF("Please run 'device list' in the shell to view all devices");
    #endif
    
    /* Special check for UART4 which we need for EMW3080 */
    const struct device *uart4 = DEVICE_DT_GET(DT_NODELABEL(uart4));
    if (uart4 != NULL) {
        LOG_INF("UART4 found via DT_NODELABEL: %s (ready: %d)", 
                uart4->name, device_is_ready(uart4));
    } else {
        LOG_ERR("UART4 not found via DT_NODELABEL");
    }
    
    LOG_INF("=== End of device list ===");
}

/* Function to list all network interfaces */
void emw3080_debug_list_interfaces(void)
{
    struct net_if *iface;
    int i = 0;
    
    LOG_INF("=== Listing all network interfaces ===");
    
    while ((iface = net_if_get_by_index(i)) != NULL) {
        const struct device *dev = net_if_get_device(iface);
        LOG_INF("Interface %d: iface=%p, device=%s", 
               i, iface, (dev != NULL) ? dev->name : "NULL");
        i++;
    }
    
    LOG_INF("=== End of interface list ===");
}

/* Function to check if EMW3080 was properly initialized */
void emw3080_debug_check_initialization(void)
{
    const struct device *uart4 = NULL;
    
    LOG_INF("=== Checking for EMW3080 device ===");
    
    /* Check for UART4 which hosts our EMW3080 */
    uart4 = device_get_binding("uart4");
    if (uart4) {
        LOG_INF("UART4 found: %s", uart4->name);
    } else {
        LOG_ERR("UART4 not found! EMW3080 cannot function without UART4");
    }
    
    /* Check UART4 status from device tree */
    LOG_INF("Checking device tree binding for EMW3080...");
    LOG_INF("For details, please use the 'device list' shell command");
    LOG_INF("and look for UART4 and any EMW3080 entries");
    
    /* Additional info about device tree compilation */
    LOG_INF("Verify that:");
    LOG_INF("1. The compatible string in overlay matches the DT_DRV_COMPAT in driver");
    LOG_INF("2. The driver is being compiled (check build files)");
    LOG_INF("3. The device tree binding is correctly processed");
    
    LOG_INF("=== EMW3080 check complete ===");
}

/* Debug function to test AT command functionality */
int emw3080_debug_at_commands(void)
{
    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("EMW3080 device not found");
        return -ENODEV;
    }
    
    struct emw3080_data *data = dev->data;
    if (!data) {
        LOG_ERR("EMW3080 device data not found");
        return -EINVAL;
    }
    
    /* If there's no UART device, we can't send AT commands */
    if (!data->uart || !device_is_ready(data->uart)) {
        LOG_ERR("UART device not available or not ready");
        return -ENODEV;
    }
    
    LOG_INF("=== Testing AT commands ===");
    
    /* Test basic AT command */
    char resp[128];
    int ret = emw3080_send_at_cmd(data, "AT\r\n", 4, resp, sizeof(resp), 2000);
    LOG_INF("AT command result: %d", ret);
    if (ret == 0) {
        LOG_INF("Response: %s", resp);
    } else {
        LOG_ERR("Failed to execute AT command");
    }
    
    /* Test setting multi-connection mode */
    LOG_INF("Testing multi-connection mode");
    char cmd[32];
    snprintf(cmd, sizeof(cmd), emw3080_cmd_set_multi_conn, 1);
    ret = emw3080_send_at_cmd(data, cmd, strlen(cmd), resp, sizeof(resp), 2000);
    LOG_INF("CIPMUX command result: %d", ret);
    if (ret == 0) {
        LOG_INF("Response: %s", resp);
    } else {
        LOG_ERR("Failed to set multi-connection mode");
    }
    
    /* Try to get IP status */
    LOG_INF("Getting IP status");
    ret = emw3080_send_at_cmd(data, "AT+CIPSTATUS\r\n", 14, resp, sizeof(resp), 2000);
    LOG_INF("CIPSTATUS command result: %d", ret);
    if (ret == 0) {
        LOG_INF("Response: %s", resp);
    } else {
        LOG_ERR("Failed to get IP status");
    }
    
    LOG_INF("=== AT command test complete ===");
    return ret;
}
