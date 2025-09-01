/* ST Microelectronics VL53L5CX Time-of-Flight sensor
 *
 * Copyright (c) 2025 David Cemin
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_VL53L5CX_VL53L5CX_H_
#define ZEPHYR_DRIVERS_SENSOR_VL53L5CX_VL53L5CX_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

/* VL53L5CX I2C registers */
#define VL53L5CX_DEVICE_ID			0x0000
#define VL53L5CX_REVISION_ID		0x0001
#define VL53L5CX_STATUS				0x0006
#define VL53L5CX_COMMAND			0x0007
#define VL53L5CX_SYSTEM_START		0x0087
#define VL53L5CX_RESULT_DISTANCE	0x0096
#define VL53L5CX_FIRMWARE_SYSTEM_STATUS	0x00E5
#define VL53L5CX_GPIO_TIO_HV_STATUS	0x0031
#define VL53L5CX_RESULT_RANGE_STATUS	0x0013
#define VL53L5CX_RESULT_FINAL_RANGE	0x0096
#define VL53L5CX_SYSTEM_INTERRUPT_CLEAR	0x0086

/* VL53L5CX constants */
#define VL53L5CX_DEVICE_ID_VAL		0xF0
#define VL53L5CX_STATUS_IDLE		0x03
#define VL53L5CX_STATUS_RANGING		0x00

/* Resolution modes */
#define VL53L5CX_RESOLUTION_4X4		16
#define VL53L5CX_RESOLUTION_8X8		64

/* Maximum zones */
#define VL53L5CX_MAX_ZONES			64

/* Custom sensor channels for multi-zone data */
enum vl53l5cx_sensor_channel {
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_0 = SENSOR_CHAN_PRIV_START,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_1,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_2,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_3,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_4,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_5,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_6,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_7,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_8,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_9,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_10,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_11,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_12,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_13,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_14,
	SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_15,
	/* Zones 16-63 for 8x8 mode... */
	SENSOR_CHAN_VL53L5CX_ZONE_COUNT = SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_0 + VL53L5CX_MAX_ZONES,
	SENSOR_CHAN_VL53L5CX_SIGNAL_RATE,
	SENSOR_CHAN_VL53L5CX_AMBIENT_RATE,
	SENSOR_CHAN_VL53L5CX_STATUS,
};

/* Zone data structure */
struct vl53l5cx_zone_data {
	uint16_t distance_mm;
	uint8_t status;
	uint16_t signal_kcps;
	uint16_t ambient_kcps;
};

struct vl53l5cx_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec int_gpio;
	struct gpio_dt_spec xshut_gpio;
	uint8_t resolution;
	uint8_t ranging_freq;
	uint8_t integration_time;
};

struct vl53l5cx_data {
	const struct device *dev;
	struct vl53l5cx_zone_data zones[VL53L5CX_MAX_ZONES];
	uint8_t zone_count;
	bool is_ranging;
	bool data_ready;
	
#ifdef CONFIG_VL53L5CX_TRIGGER
	const struct device *gpio_dev;
	struct gpio_callback gpio_cb;
	sensor_trigger_handler_t data_ready_handler;
	const struct sensor_trigger *trigger;
	
#if defined(CONFIG_VL53L5CX_TRIGGER_OWN_THREAD)
	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_VL53L5CX_THREAD_STACK_SIZE);
	struct k_thread thread;
	struct k_sem gpio_sem;
#elif defined(CONFIG_VL53L5CX_TRIGGER_GLOBAL_THREAD)
	struct k_work work;
#endif /* CONFIG_VL53L5CX_TRIGGER_OWN_THREAD */
#endif /* CONFIG_VL53L5CX_TRIGGER */
};

/* Function prototypes */
int vl53l5cx_init(const struct device *dev);
int vl53l5cx_sample_fetch(const struct device *dev, enum sensor_channel chan);
int vl53l5cx_channel_get(const struct device *dev, enum sensor_channel chan,
			 struct sensor_value *val);
int vl53l5cx_attr_set(const struct device *dev, enum sensor_channel chan,
		      enum sensor_attribute attr, const struct sensor_value *val);
int vl53l5cx_attr_get(const struct device *dev, enum sensor_channel chan,
		      enum sensor_attribute attr, struct sensor_value *val);

#ifdef CONFIG_VL53L5CX_TRIGGER
int vl53l5cx_trigger_set(const struct device *dev,
			 const struct sensor_trigger *trigger,
			 sensor_trigger_handler_t handler);
int vl53l5cx_init_interrupt(const struct device *dev);
#endif

/* Internal helper functions - declared for trigger file usage */
int vl53l5cx_read_results(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_SENSOR_VL53L5CX_VL53L5CX_H_ */
