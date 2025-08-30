#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include "../drivers/wifi/emw3080/emw3080_debug.h"
#include "../drivers/wifi/emw3080/emw3080_test.h"
#include "../drivers/wifi/emw3080/emw3080_offload.h"
#include "emw3080_network.h"
#include "emw3080_init.h"

/* EMW3080 test function declarations */
extern int emw3080_spi_basic_test(void);
extern int emw3080_spi_init_basic(void);
extern int emw3080_hci_comprehensive_test(void);

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Display application banner with version */
static void print_app_banner(void)
{
    LOG_INF("****************************************");
    LOG_INF("*     EMW3080 WiFi Sample v1.0.0      *");
    LOG_INF("*  STM32U585 + EMW3080 WiFi Module    *");
    LOG_INF("****************************************");
    LOG_INF("Build: %s %s", __DATE__, __TIME__);

    /* Print basic system information */
    LOG_INF("System Information:");
    LOG_INF("- Zephyr Version: %s", KERNEL_VERSION_STRING);
    LOG_INF("- Board: %s", CONFIG_BOARD);
}

int main(void)
{
    /* Print application banner */
    print_app_banner();

    /* Give devices time to initialize */
    LOG_INF("Starting with extended boot delay for safety");
    k_sleep(K_SECONDS(2));
    
    /* Ensure EMW3080 device is properly initialized */
    LOG_WRN("Step 0: Ensuring EMW3080 device registration...");
    int ret = emw3080_ensure_device_ready();
    if (ret == 0) {
        LOG_INF("✅ EMW3080 device registration verified");
        emw3080_print_device_info();
    } else {
        LOG_ERR("❌ EMW3080 device registration failed: %d", ret);
        LOG_ERR("This indicates a device tree or driver initialization issue");
        goto cleanup;
    }
    
    /* SPI → MX WiFi Protocol → HCI testing path */
    LOG_INF("Starting SPI communication path testing...");
    LOG_INF("Test sequence: SPI → MX WiFi Protocol → HCI");
    
    /* Step 1: Initialize SPI interface */
    LOG_INF("Step 1: Initializing SPI interface...");
    ret = emw3080_spi_init_basic();
    if (ret == 0) {
        LOG_INF("✅ SPI interface initialized successfully");
    } else {
        LOG_ERR("❌ SPI interface initialization failed: %d", ret);
        goto cleanup;
    }
    
    /* Step 2: Test SPI with MX WiFi protocol */
    LOG_WRN("Step 2: Testing SPI communication with MX WiFi protocol...");
    ret = emw3080_spi_basic_test();
    if (ret == 0) {
        LOG_INF("✅ SPI + MX WiFi protocol test PASSED");
    } else {
        LOG_ERR("❌ SPI + MX WiFi protocol test FAILED: %d", ret);
        goto cleanup;
    }
    
    /* Step 3: Test HCI layer using SPI */
    LOG_WRN("Step 3: Testing HCI layer (using SPI + MX WiFi protocol)...");
    ret = emw3080_hci_comprehensive_test();
    if (ret == 0) {
        LOG_INF("✅ HCI layer test PASSED");
    } else {
        LOG_ERR("❌ HCI layer test FAILED: %d", ret);
        goto cleanup;
    }
    
    LOG_INF("🎉 SPI communication path validation completed successfully!");
    LOG_INF("SPI ✅ | MX WiFi Protocol ✅ | HCI ✅ | Ready for WiFi management");
    
    /* Initialize network management for shell commands (optional) */
    LOG_INF("Initializing network management for shell support...");
    ret = emw3080_network_init();
    if (ret == 0) {
        LOG_INF("✅ Network management initialized");
        
        /* Setup interface for shell usage */
        ret = emw3080_network_setup_interface();
        if (ret == 0) {
            LOG_INF("✅ WiFi interface configured for shell usage");
        } else {
            LOG_WRN("⚠️  WiFi interface setup failed, shell may have limited functionality");
        }
    } else {
        LOG_WRN("⚠️  Network management init failed, shell may have limited functionality");
    }

cleanup:
    LOG_INF("Bottom-up testing complete - ready for shell commands");
    return 0;
}
