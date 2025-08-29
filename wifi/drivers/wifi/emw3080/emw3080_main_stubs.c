/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal stub implementations for main.c requirements
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_main_stubs, CONFIG_LOG_DEFAULT_LEVEL);

/* Debug function stubs - these are only called by main.c for diagnostics */
void emw3080_debug_list_devices(void)
{
    LOG_INF("EMW3080 Debug: List devices (using real SPI implementation)");
}

void emw3080_debug_list_interfaces(void) 
{
    LOG_INF("EMW3080 Debug: List interfaces (using real SPI implementation)");
}

void emw3080_debug_check_initialization(void)
{
    LOG_INF("EMW3080 Debug: Check initialization (using real SPI implementation)");
}

int emw3080_debug_at_commands(void)
{
    LOG_INF("EMW3080 Debug: AT commands (using real SPI implementation)");
    return 0;
}

/* Fallback function stub */
int emw3080_fallback_init(void)
{
    LOG_INF("EMW3080 Fallback: Init (using real SPI implementation)");
    return 0;
}
