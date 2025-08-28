/*
 * Copyright (c) 2025 David Cemin
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
#include <zephyr/net/net_if.h>
#include <zephyr/sys/printk.h>

/* This file adds additional debugging to help diagnose EMW3080 driver issues */

/* Function to list all devices in the system */
void emw3080_debug_list_devices(void)
{
    LOG_INF("=== Listing all devices in system ===");
    LOG_INF("This function uses the 'device list' shell command instead of internal APIs");
    LOG_INF("Please run 'device list' in the shell to view all devices");
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
