/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_HW_H
#define EMW3080_HW_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "emw3080.h"

/* Perform a hardware reset of the EMW3080 module if possible */
int emw3080_hw_reset(struct emw3080_data *data);

/* Initialize the EMW3080 hardware */
int emw3080_hw_init(struct emw3080_data *data);

#endif /* EMW3080_HW_H */
