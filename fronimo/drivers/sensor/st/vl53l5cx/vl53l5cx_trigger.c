/* ST Microelectronics VL53L5CX Time-of-Flight sensor - Trigger support
 *
 * Copyright (c) 2025 David Cemin
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_vl53l5cx

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "vl53l5cx.h"

LOG_MODULE_DECLARE(vl53l5cx, CONFIG_SENSOR_LOG_LEVEL);

static void vl53l5cx_gpio_callback(const struct device *dev,
				   struct gpio_callback *cb,
				   uint32_t pins)
{
	struct vl53l5cx_data *data = CONTAINER_OF(cb, struct vl53l5cx_data, gpio_cb);
	const struct device *sensor = data->dev;
	
	ARG_UNUSED(dev);
	ARG_UNUSED(pins);
	
	/* Signal the work queue to handle the interrupt */
	k_work_submit(&data->work);
	
	LOG_DBG("VL53L5CX interrupt triggered");
}

static void vl53l5cx_work_cb(struct k_work *work)
{
	struct vl53l5cx_data *data = CONTAINER_OF(work, struct vl53l5cx_data, work);
	const struct device *dev = data->dev;
	
	/* Read the sensor data */
	int ret = vl53l5cx_read_results(dev);
	if (ret < 0) {
		LOG_ERR("Failed to read results in interrupt: %d", ret);
		return;
	}
	
	/* Call the trigger handler if set */
	if (data->data_ready_handler != NULL) {
		struct sensor_trigger trigger = {
			.type = SENSOR_TRIG_DATA_READY,
			.chan = SENSOR_CHAN_DISTANCE,
		};
		
		data->data_ready_handler(dev, &trigger);
	}
}

int vl53l5cx_trigger_set(const struct device *dev,
			 const struct sensor_trigger *trig,
			 sensor_trigger_handler_t handler)
{
	struct vl53l5cx_data *data = dev->data;
	const struct vl53l5cx_config *cfg = dev->config;
	int ret;
	
	if (trig->type != SENSOR_TRIG_DATA_READY) {
		LOG_ERR("Unsupported trigger type %d", trig->type);
		return -ENOTSUP;
	}
	
	if (!gpio_is_ready_dt(&cfg->int_gpio)) {
		LOG_ERR("Interrupt GPIO not ready");
		return -ENODEV;
	}
	
	/* Disable interrupt during configuration */
	ret = gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_DISABLE);
	if (ret < 0) {
		LOG_ERR("Failed to disable interrupt: %d", ret);
		return ret;
	}
	
	data->data_ready_handler = handler;
	
	if (handler == NULL) {
		LOG_INF("VL53L5CX trigger disabled");
		return 0;
	}
	
	/* Enable interrupt */
	ret = gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to enable interrupt: %d", ret);
		return ret;
	}
	
	LOG_INF("VL53L5CX trigger enabled");
	
	return 0;
}

int vl53l5cx_init_interrupt(const struct device *dev)
{
	const struct vl53l5cx_config *cfg = dev->config;
	struct vl53l5cx_data *data = dev->data;
	int ret;
	
	if (!gpio_is_ready_dt(&cfg->int_gpio)) {
		LOG_INF("Interrupt GPIO not configured, skipping trigger init");
		return 0;
	}
	
	/* Configure interrupt GPIO as input */
	ret = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure interrupt GPIO: %d", ret);
		return ret;
	}
	
	/* Initialize work queue for interrupt handling */
	k_work_init(&data->work, vl53l5cx_work_cb);
	
	/* Initialize GPIO callback */
	gpio_init_callback(&data->gpio_cb, vl53l5cx_gpio_callback,
			   BIT(cfg->int_gpio.pin));
	
	/* Add callback */
	ret = gpio_add_callback(cfg->int_gpio.port, &data->gpio_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add GPIO callback: %d", ret);
		return ret;
	}
	
	LOG_INF("VL53L5CX interrupt initialized");
	
	return 0;
}
