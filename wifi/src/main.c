#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include "../drivers/wifi/emw3080/emw3080_debug.h"
#include "../drivers/wifi/emw3080/emw3080_test.h"
#include "../drivers/wifi/emw3080/emw3080_offload.h"
#include "emw3080_network.h"

/* EMW3080 test function declarations */
extern int slip_validation_test(void);
extern int emw3080_spi_basic_test(void);
extern int emw3080_spi_init_basic(void);

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
    
    /* Bottom-up testing: SPI → SLIP validation */
    LOG_INF("Starting bottom-up testing: SPI → SLIP layers...");
    
    /* Step 1: Bring up SPI interface if needed */
    LOG_INF("Step 1: Initializing SPI interface...");
    int ret = emw3080_spi_init_basic();
    if (ret == 0) {
        LOG_INF("✅ SPI interface initialized successfully");
    } else {
        LOG_ERR("❌ SPI interface initialization failed: %d", ret);
        goto cleanup;
    }
    
    /* Step 2: Test SPI interface */
    LOG_INF("Step 2: Testing SPI interface...");
    ret = emw3080_spi_basic_test();
    if (ret == 0) {
        LOG_INF("✅ SPI interface test PASSED");
    } else {
        LOG_ERR("❌ SPI interface test FAILED: %d", ret);
        goto cleanup;
    }
    
    /* Step 3: Bring up SLIP if needed */
    LOG_INF("Step 3: Initializing SLIP protocol...");
    // SLIP is a protocol layer, no specific initialization needed beyond what's in the test
    LOG_INF("✅ SLIP protocol ready (stateless protocol)");
    
    /* Step 4: Test SLIP */
    LOG_INF("Step 4: Testing SLIP protocol...");
    ret = slip_validation_test();
    if (ret == 0) {
        LOG_INF("✅ SLIP protocol test PASSED");
    } else {
        LOG_ERR("❌ SLIP protocol test FAILED: %d", ret);
        goto cleanup;
    }
    
    LOG_INF("🎉 Bottom-up validation completed successfully!");
    LOG_INF("SPI ✅ | SLIP ✅ | Ready for next layer integration");
    
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
