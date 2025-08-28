/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_OFFLOAD_H
#define EMW3080_OFFLOAD_H

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_offload.h>

/* Ethernet L2 compatible send function that can be called directly by L2 */
int emw3080_offload_send_pkt(struct net_if *iface, struct net_pkt *pkt);

/* Define the offload API */
extern const struct net_offload emw3080_offload;

#endif /* EMW3080_OFFLOAD_H */
