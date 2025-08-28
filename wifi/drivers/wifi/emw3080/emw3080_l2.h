/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_L2_H
#define EMW3080_L2_H

#include <zephyr/net/net_if.h>

/* Function to attach L2 interface to the WiFi interface */
int emw3080_attach_l2_to_iface(struct net_if *iface);

/* Function to enable direct communication mode */
int emw3080_enable_direct_mode(struct net_if *iface);

#endif /* EMW3080_L2_H */
