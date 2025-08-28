/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_SOCKET_H
#define EMW3080_SOCKET_H

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include "emw3080.h"

/* AT commands for connection management */
#define EMW3080_CMD_START_TCP_WITH_ID "AT+CIPSTART=%d,\"TCP\",\"%s\",%d\r\n"
#define EMW3080_CMD_START_UDP_WITH_ID "AT+CIPSTART=%d,\"UDP\",\"%s\",%d\r\n"
#define EMW3080_CMD_SET_DHCP "AT+CWDHCP=1,1\r\n"
#define EMW3080_CMD_GET_IP "AT+CIFSR\r\n"

/* Send a packet via AT commands */
int emw3080_send_pkt(struct net_if *iface, struct net_pkt *pkt);

/* Process received data from +IPD messages */
void emw3080_process_ipd(struct emw3080_data *data, const uint8_t *ipd_data, uint16_t len);

#endif /* EMW3080_SOCKET_H */
