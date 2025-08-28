/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_DHCP_H
#define EMW3080_DHCP_H

#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <stdbool.h>

/* Function to handle DHCP packets specifically */
int emw3080_handle_dhcp(struct net_if *iface, struct net_pkt *pkt);

/* Function to check if a packet is DHCP */
bool emw3080_is_dhcp_packet(struct net_pkt *pkt);

#endif /* EMW3080_DHCP_H */
