/**
 * @file emw3080_init.c
 * @brief EMW3080 Device Initialization and Registration
 * 
 * This module ensures the EMW3080 device is properly initialized and registered
 * before any testing or usage begins. It provides functions to verify device
 * registration and initialize the hardware if needed.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include "../drivers/wifi/emw3080/emw3080.h"

LOG_MODULE_REGISTER(emw3080_init, CONFIG_LOG_DEFAULT_LEVEL);

/* Forward declarations */
void emw3080_print_device_info(void);

/**
 * Initialize and verify EMW3080 device registration
 * @return 0 on success, negative error code on failure
 */
int emw3080_ensure_device_ready(void)
{
    LOG_INF("🔧 Ensuring EMW3080 device is initialized and ready...");
    
    /* First, check if the device is already registered */
    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("❌ EMW3080 device not found in system");
        LOG_ERR("This indicates the device tree node is not properly compiled");
        LOG_ERR("or the driver is not being instantiated correctly");
        return -ENODEV;
    }
    
    LOG_INF("✅ EMW3080 device found: %s", dev->name);
    
    /* Check if the device is ready */
    if (!device_is_ready(dev)) {
        LOG_ERR("❌ EMW3080 device is not ready - initialization failed");
        LOG_ERR("Device state: %s", device_is_ready(dev) ? "READY" : "NOT_READY");
        LOG_ERR("Checking hardware initialization failure...");
        emw3080_print_device_info();
        return -ENODEV;
    }
    
    LOG_INF("✅ EMW3080 device is ready and initialized");
    
    /* Try to get device data to verify structure */
    void *data = (void *)dev->data;
    if (!data) {
        LOG_WRN("⚠️  EMW3080 device has no data structure");
    } else {
        LOG_DBG("✅ EMW3080 device data structure present");
    }
    
    /* Check if device has API */
    if (!dev->api) {
        LOG_WRN("⚠️  EMW3080 device has no API structure");
    } else {
        LOG_DBG("✅ EMW3080 device API structure present");
    }
    
    LOG_INF("🎉 EMW3080 device is fully ready for use");
    return 0;
}

/**
 * Print detailed EMW3080 device information for debugging
 */
void emw3080_print_device_info(void)
{
    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("No EMW3080 device to print info for");
        return;
    }
    
    LOG_INF("=== EMW3080 Device Information ===");
    LOG_INF("Device Name: %s", dev->name ? dev->name : "Unknown");
    LOG_INF("Device Ready: %s", device_is_ready(dev) ? "YES" : "NO");
    LOG_INF("Device Data: %p", dev->data);
    LOG_INF("Device API: %p", dev->api);
    
    /* Try to get device ordinal for debugging */
    int ordinal = 0;
    STRUCT_SECTION_FOREACH(device, device_iter) {
        if (device_iter == dev) {
            LOG_INF("Device Ordinal: %d", ordinal);
            break;
        }
        ordinal++;
    }
    
    LOG_INF("==================================");
}

/**
 * Force device initialization if it hasn't happened automatically
 * This should only be used as a last resort
 */
int emw3080_force_device_init(void)
{
    LOG_WRN("🔧 Attempting to force EMW3080 device initialization...");
    
    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("❌ Cannot force init - device not found in system");
        return -ENODEV;
    }
    
    /* If the device has an init function, we could try calling it */
    /* But this is dangerous since Zephyr manages device initialization */
    LOG_WRN("⚠️  Force initialization not implemented - this could cause conflicts");
    LOG_WRN("The device should be initialized automatically by Zephyr");
    
    return -ENOTSUP;
}
