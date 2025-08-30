/*
 * EMW3080 Network Management Header
 * 
 * This file contains declarations for WiFi, DHCP, and network interface
 * management functions that were moved out of main.c.
 */

#ifndef EMW3080_NETWORK_H
#define EMW3080_NETWORK_H

#include <zephyr/net/net_if.h>

/* Network management function declarations */

/**
 * @brief Initialize network management callbacks
 * 
 * Sets up WiFi and DHCP event handlers for the EMW3080 module.
 * This should be called during system initialization.
 * 
 * @return 0 on success, negative error code on failure
 */
int emw3080_network_init(void);

/**
 * @brief Setup WiFi interface for operation
 * 
 * Finds the WiFi interface, brings it up, and configures it for operation.
 * Also prints usage instructions for WiFi shell commands.
 * 
 * @return 0 on success, negative error code on failure
 */
int emw3080_network_setup_interface(void);

/**
 * @brief Get the WiFi network interface
 * 
 * Helper function to locate and return the WiFi network interface.
 * Prefers EMW3080 interfaces but will fall back to generic WiFi interfaces.
 * 
 * @return Pointer to the WiFi interface, or NULL if none found
 */
struct net_if *get_wifi_iface(void);

#endif /* EMW3080_NETWORK_H */
