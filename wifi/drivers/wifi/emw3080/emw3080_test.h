/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_TEST_H
#define EMW3080_TEST_H

/* Function to test WiFi L2 implementation */
void test_wifi_l2_init(void);

/* Test AT command functionality */
int emw3080_test_at_commands(void);

/* HCI Layer Test Functions */
int emw3080_hci_init_test(void);
int emw3080_hci_system_test(void);
int emw3080_hci_wifi_test(void);
int emw3080_hci_comprehensive_test(void);
int emw3080_hci_stress_test(void);

#endif /* EMW3080_TEST_H */
