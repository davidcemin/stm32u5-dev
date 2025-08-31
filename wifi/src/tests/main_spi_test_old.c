#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include "../emw3080_init.h"

/* EMW3080 test function declarations */
extern int emw3080_spi_basic_test(void);
extern int emw3080_spi_init_basic(void);

LOG_MODULE_REGISTER(main_spi_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Display application banner with version */
static void print_app_banner(void)
{
    LOG_INF("****************************************");
    LOG_INF("*     EMW3080 SPI Test v1.0.0         *");
    LOG_INF("*  Full-Duplex SPI Integration Test   *");
    LOG_INF("****************************************");
    LOG_INF("Build: %s %s", __DATE__, __TIME__);

    /* Print basic system information */
    LOG_INF("System Information:");
    LOG_INF("- Zephyr Version: %s", KERNEL_VERSION_STRING);
    LOG_INF("- Board: %s", CONFIG_BOARD);
}

/* EMW3080 Device Initialization (like in main.c) */
static int emw3080_device_init(void)
{
    LOG_INF("=== EMW3080 Device Initialization ===");
    
    /* Ensure EMW3080 device is properly initialized */
    LOG_INF("Ensuring EMW3080 device registration...");
    int ret = emw3080_ensure_device_ready();
    if (ret == 0) {
        LOG_INF("✅ EMW3080 device registration verified");
        emw3080_print_device_info();
    } else {
        LOG_ERR("❌ EMW3080 device registration failed: %d", ret);
        LOG_ERR("This indicates a device tree or driver initialization issue");
        return ret;
    }
    
    LOG_INF("EMW3080 device initialization complete");
    return 0;
}

/* Basic SPI communication test using full-duplex pattern */

/* EMW3080 Hardware Reset and Initialization */
static int emw3080_hardware_reset(void)
{
    LOG_INF("=== EMW3080 Hardware Reset Sequence ===");
    
    /* Get device node from devicetree */
    static const struct gpio_dt_spec reset_gpio = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(emw3080), reset_gpios, {0});
    static const struct gpio_dt_spec wakeup_gpio = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(emw3080), wakeup_gpios, {0});
    
    LOG_INF("Checking GPIO specifications...");
    LOG_INF("Reset GPIO port: %p", reset_gpio.port);
    LOG_INF("Wakeup GPIO port: %p", wakeup_gpio.port);
    
    /* Check if EMW3080 node exists in devicetree */
    #if DT_NODE_EXISTS(DT_NODELABEL(emw3080))
        LOG_INF("✅ EMW3080 devicetree node found");
    #else
        LOG_ERR("❌ EMW3080 devicetree node NOT found");
        LOG_ERR("This means the overlay is not being applied correctly");
        return -ENODEV;
    #endif
    
    /* Configure reset GPIO if available */
    if (reset_gpio.port) {
        LOG_INF("Configuring reset GPIO (PF15)...");
        if (!gpio_is_ready_dt(&reset_gpio)) {
            LOG_ERR("Reset GPIO device not ready");
            return -ENODEV;
        }
        
        int ret = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure reset GPIO: %d", ret);
            return ret;
        }
        
        /* Perform hardware reset sequence */
        LOG_INF("Asserting EMW3080 reset...");
        gpio_pin_set_dt(&reset_gpio, 1);  /* Assert reset (active low) */
        k_sleep(K_MSEC(10));               /* Hold reset for 10ms */
        
        LOG_INF("Releasing EMW3080 reset...");
        gpio_pin_set_dt(&reset_gpio, 0);  /* Release reset */
        k_sleep(K_MSEC(100));              /* Wait for module to boot */
        
        LOG_INF("✅ Hardware reset completed");
    } else {
        LOG_WRN("⚠️  Reset GPIO not configured in device tree");
        LOG_WRN("This is expected if the EMW3080 node is missing");
    }
    
    /* Configure wakeup/ready GPIO if available */
    if (wakeup_gpio.port) {
        LOG_INF("Configuring wakeup/ready GPIO (PG15)...");
        if (!gpio_is_ready_dt(&wakeup_gpio)) {
            LOG_ERR("Wakeup GPIO device not ready");
            return -ENODEV;
        }
        
        int ret = gpio_pin_configure_dt(&wakeup_gpio, GPIO_INPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure wakeup GPIO: %d", ret);
            return ret;
        }
        
        /* Check if module is signaling ready */
        int ready_state = gpio_pin_get_dt(&wakeup_gpio);
        LOG_INF("Module ready signal: %s", ready_state ? "HIGH" : "LOW");
    } else {
        LOG_WRN("⚠️  Wakeup GPIO not configured in device tree");
        LOG_WRN("This is expected if the EMW3080 node is missing");
    }
    
    LOG_INF("EMW3080 hardware initialization complete");
    return 0;
}

int main(void)
{
    int ret;
    
    print_app_banner();
    
    /* Step 0: EMW3080 Device Initialization */
    LOG_INF("=== STEP 0: EMW3080 Device Initialization ===");
    ret = emw3080_device_init();
    if (ret != 0) {
        LOG_ERR("❌ Device initialization failed: %d (continuing anyway)", ret);
        /* Continue with testing even if init fails */
    } else {
        LOG_INF("✅ Device initialization successful");
    }
    
    /* Short delay for module stabilization */
    k_sleep(K_MSEC(200));
    
    /* Step 1: Initialize SPI communication */
    LOG_INF("=== STEP 1: SPI Initialization ===");
    ret = emw3080_spi_init_basic();
    if (ret != 0) {
        LOG_ERR("❌ SPI initialization failed: %d", ret);
        return ret;
    }
    LOG_INF("✅ SPI initialization successful");
    
    /* Short delay to ensure stable communication */
    k_sleep(K_MSEC(100));
    
    /* Step 2: Full-Duplex SPI Testing */
    LOG_INF("=== STEP 2: Full-Duplex SPI Communication Test ===");
    LOG_INF("Testing ST MX WiFi protocol compatibility...");
    
    ret = emw3080_spi_basic_test();
    if (ret == 0) {
        LOG_INF("✅ Full-duplex SPI test PASSED");
        LOG_INF("🎉 ST MX WiFi protocol integration successful!");
    } else {
        LOG_ERR("❌ Full-duplex SPI test FAILED: %d", ret);
        return ret;
    }
    
    LOG_INF("=== SPI Integration Test Complete ===");
    LOG_INF("✅ Your full-duplex implementation matches ST's pattern");
    LOG_INF("✅ Ready for higher-level protocol integration");
    LOG_INF("✅ Bottom-up testing approach validated");
    
    /* Stay alive for debugging/monitoring */
    while (1) {
        k_sleep(K_MSEC(1000));
    }
    
    return 0;
}
