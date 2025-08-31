/* Minimal bring-up for EMW3080 in SPI bypass mode */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>

#include "../drivers/wifi/emw3080/emw3080.h"
#include "../drivers/wifi/emw3080/emw3080_ipc.h"
#include "emw3080_init.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static void banner(void)
{
    LOG_INF("EMW3080 SPI bypass bring-up (Board:%s, Zephyr:%s)", CONFIG_BOARD, KERNEL_VERSION_STRING);
}

int main(void)
{
    banner();
    k_sleep(K_MSEC(500));

    /* Ensure device is present and ready */
    if (emw3080_ensure_device_ready() != 0) {
        LOG_ERR("EMW3080 device not ready (check DT overlay and SPI)");
        return -ENODEV;
    }

    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("No EMW3080 device instance found");
        return -ENODEV;
    }

    /* Device is initialized by the driver at POST_KERNEL; avoid extra resets here */

    /* Query firmware version */
    char version[64] = {0};
    int ret = emw3080_ipc_get_version(dev, version, sizeof(version));
    if (ret == 0) {
        LOG_INF("FW version: %s", version);
    } else {
        LOG_WRN("Failed to read FW version (%d)", ret);
    }

    /* Query MAC address */
    uint8_t mac[6] = {0};
    ret = emw3080_ipc_get_mac(dev, mac);
    if (ret == 0) {
        LOG_INF("MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        LOG_WRN("Failed to read MAC (%d)", ret);
    }

    /* Try echo */
    char echo_out[32] = {0};
    if (emw3080_ipc_echo(dev, "PING", echo_out, sizeof(echo_out)) == 0) {
        LOG_INF("Echo: %s", echo_out);
    } else {
        LOG_WRN("Echo failed");
    }

    /* Try get IP */
    uint8_t ip[4] = {0};
    if (emw3080_ipc_get_ip(dev, ip) == 0) {
        LOG_INF("IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    } else {
        LOG_WRN("Get IP failed");
    }

    LOG_INF("Bring-up complete. Idling...");
    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
