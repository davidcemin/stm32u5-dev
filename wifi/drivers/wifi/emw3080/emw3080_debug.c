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
    
    LOG_INF("=== Testing EMW3080 AT commands ===");
    
    /* Configure the UART first */
    LOG_INF("Configuring UART for AT commands");
    extern int emw3080_configure_uart(const struct device *uart_dev);
    int ret = emw3080_configure_uart(data->uart);
    if (ret < 0) {
        LOG_ERR("Failed to configure UART: %d", ret);
        return ret;
    }
    
    /* Flush any pending data */
    extern void emw3080_uart_flush_rx(const struct device *uart_dev);
    emw3080_uart_flush_rx(data->uart);
    
    /* Test basic AT command with increasing timeouts */
    char resp[256];
    bool success = false;
    int timeouts[] = {1000, 2000, 5000, 10000};
    
    LOG_INF("Testing basic AT command with increasing timeouts");
    for (int i = 0; i < ARRAY_SIZE(timeouts); i++) {
        memset(resp, 0, sizeof(resp));
        LOG_INF("Attempt %d with timeout %d ms", i+1, timeouts[i]);
        
        ret = emw3080_send_at_cmd(data, "AT\r\n", 4, resp, sizeof(resp), timeouts[i]);
        if (ret == 0) {
            LOG_INF("AT command successful! Response: %s", resp);
            success = true;
            break;
        } else {
            LOG_WRN("AT command failed with timeout %d ms: %d", timeouts[i], ret);
        }
        
        /* Reset the module if we're on the last attempt and still failing */
        if (i == ARRAY_SIZE(timeouts) - 2) {
            LOG_WRN("AT commands failing, attempting module reset");
            if (data->reset_gpio.port != NULL) {
                /* Hard reset the module */
                LOG_INF("Performing hard reset of EMW3080");
                gpio_pin_set_dt(&data->reset_gpio, 1);
                k_sleep(K_MSEC(100));
                gpio_pin_set_dt(&data->reset_gpio, 0);
                k_sleep(K_MSEC(1000));  /* Extra delay after reset */
                emw3080_uart_flush_rx(data->uart);
            } else {
                LOG_WRN("Reset GPIO not available for hardware reset");
            }
        }
        
        /* Short pause between attempts */
        k_sleep(K_MSEC(100));
    }
    
    if (!success) {
        LOG_ERR("Failed to execute basic AT command after multiple attempts");
        LOG_ERR("This indicates a communication problem with the EMW3080 module");
        LOG_ERR("Check hardware connections, reset sequence, and UART config");
        return -EIO;
    }
    
    /* Test firmware version command */
    LOG_INF("Testing firmware version command");
    ret = emw3080_send_at_cmd(data, "AT+GMR\r\n", 8, resp, sizeof(resp), 5000);
    if (ret == 0) {
        LOG_INF("Firmware version: %s", resp);
    } else {
        LOG_WRN("Failed to get firmware version: %d", ret);
    }
    
    /* Test setting WiFi mode */
    LOG_INF("Setting WiFi mode to station mode");
    ret = emw3080_send_at_cmd(data, "AT+CWMODE=1\r\n", 13, resp, sizeof(resp), 5000);
    if (ret == 0) {
        LOG_INF("WiFi mode set to station mode");
    } else {
        LOG_WRN("Failed to set WiFi mode: %d", ret);
    }
    
    /* Test setting multi-connection mode */
    LOG_INF("Setting multi-connection mode");
    ret = emw3080_send_at_cmd(data, "AT+CIPMUX=1\r\n", 13, resp, sizeof(resp), 5000);
    if (ret == 0) {
        LOG_INF("Multi-connection mode enabled");
    } else {
        LOG_WRN("Failed to set multi-connection mode: %d", ret);
    }
    
    /* Try to get IP status */
    LOG_INF("Getting IP status");
    ret = emw3080_send_at_cmd(data, "AT+CIPSTATUS\r\n", 14, resp, sizeof(resp), 5000);
    if (ret == 0) {
        LOG_INF("IP status: %s", resp);
    } else {
        LOG_WRN("Failed to get IP status: %d", ret);
    }
    
    /* Scan for networks */
    LOG_INF("Scanning for WiFi networks");
    ret = emw3080_send_at_cmd(data, "AT+CWLAP\r\n", 10, resp, sizeof(resp), 10000);
    if (ret == 0) {
        LOG_INF("Network scan results (truncated):");
        /* Only print the first part as the response might be very long */
        char shortened[100];
        strncpy(shortened, resp, sizeof(shortened) - 1);
        shortened[sizeof(shortened) - 1] = '\0';
        LOG_INF("%s...", shortened);
    } else {
        LOG_WRN("Failed to scan for networks: %d", ret);
    }
    
    /* Get the module's MAC address */
    LOG_INF("Getting MAC address");
    ret = emw3080_send_at_cmd(data, "AT+CIPSTAMAC?\r\n", 15, resp, sizeof(resp), 5000);
    if (ret == 0) {
        LOG_INF("MAC address: %s", resp);
    } else {
        LOG_WRN("Failed to get MAC address: %d", ret);
    }
    
    LOG_INF("=== AT command diagnostics complete ===");
    return success ? 0 : -EIO;
}
