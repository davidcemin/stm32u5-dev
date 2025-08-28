/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_L2_H
#define EMW3080_L2_H

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>

/* Function to attach L2 interface to the WiFi interface */
int emw3080_attach_l2_to_iface(struct net_if *iface);

/* Function to enable direct communication mode */
int emw3080_enable_direct_mode(struct net_if *iface);

/* Function to send packets through the WiFi interface */
int emw3080_send_pkt(struct net_if *iface, struct net_pkt *pkt);

/* Initialize the L2 layer */
void emw3080_l2_init(void);

#endif /* EMW3080_L2_H */
