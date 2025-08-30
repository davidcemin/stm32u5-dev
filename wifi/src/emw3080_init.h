/**
 * @file emw3080_init.h
 * @brief EMW3080 Device Initialization and Registration Header
 */

#ifndef EMW3080_INIT_H
#define EMW3080_INIT_H

/**
 * Initialize and verify EMW3080 device registration
 * @return 0 on success, negative error code on failure
 */
int emw3080_ensure_device_ready(void);

/**
 * Print detailed EMW3080 device information for debugging
 */
void emw3080_print_device_info(void);

/**
 * Force device initialization if it hasn't happened automatically
 * This should only be used as a last resort
 * @return 0 on success, negative error code on failure
 */
int emw3080_force_device_init(void);

/* Optional delayed init hook implemented in the driver */
int emw3080_delayed_init(void);

#endif /* EMW3080_INIT_H */
