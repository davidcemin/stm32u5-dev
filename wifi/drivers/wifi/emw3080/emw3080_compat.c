/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "emw3080.h"
#include "emw3080_spi.h"

LOG_MODULE_REGISTER(emw3080_compat, CONFIG_WIFI_LOG_LEVEL);

/* Stub implementations for legacy MIPC functions called by wifi_shell.c
 * These allow the project to build while transitioning to the new SLIP-enhanced IPC
 */

int emw3080_mipc_spi_init(void)
{
    LOG_INF("MIPC Stub: SPI init - now using SLIP-enhanced IPC instead");
    return 0;
}

int emw3080_mipc_spi_poll(void)
{
    LOG_DBG("MIPC Stub: SPI poll - now using SLIP-enhanced IPC instead");
    return 0;
}

int mipc_echo(const char *data)
{
    LOG_INF("MIPC Stub: Echo '%s' - now using SLIP-enhanced IPC instead", data ? data : "(null)");
    return 0;
}

int mipc_request(int cmd, const void *data, size_t len, void *response, size_t resp_len)
{
    LOG_INF("MIPC Stub: Request cmd=0x%x - now using SLIP-enhanced IPC instead", cmd);
    return 0;
}

/* Stub for missing DHCP packet function */
int emw3080_send_dhcp_packet(const struct device *dev, struct net_pkt *pkt)
{
    LOG_DBG("EMW3080: DHCP packet send stub - packet handling via SLIP protocol");
    /* In a full implementation, this would send DHCP packets through the SLIP-enhanced interface */
    return 0;
}

/* Device access function stubs for wifi_shell.c */
const struct device *get_emw3080_device(void)
{
    LOG_DBG("Compatibility stub: get_emw3080_device() - no device in SLIP test mode");
    return NULL;  /* Return NULL since we're not using actual device in bottom-up testing */
}

const struct device *get_emw3080_net_device(void)
{
    LOG_DBG("Compatibility stub: get_emw3080_net_device() - no network device in SLIP test mode");
    return NULL;  /* Return NULL since we're not using network device in bottom-up testing */
}
