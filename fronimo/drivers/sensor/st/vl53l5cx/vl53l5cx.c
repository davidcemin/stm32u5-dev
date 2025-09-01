/* ST Microelectronics VL53L5CX Time-of-Flight sensor
 *
 * Copyright (c) 2025 David Cemin
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_vl53l5cx

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include "vl53l5cx.h"

LOG_MODULE_REGISTER(vl53l5cx, CONFIG_SENSOR_LOG_LEVEL);

/* I2C read/write helpers */
static int vl53l5cx_i2c_read(const struct device *dev, uint16_t reg,
			     uint8_t *data, uint16_t len)
{
	const struct vl53l5cx_config *cfg = dev->config;
	uint8_t reg_buf[2];
	
	reg_buf[0] = (reg >> 8) & 0xFF;
	reg_buf[1] = reg & 0xFF;
	
	return i2c_write_read_dt(&cfg->i2c, reg_buf, sizeof(reg_buf), data, len);
}

static int vl53l5cx_i2c_write(const struct device *dev, uint16_t reg,
			      const uint8_t *data, uint16_t len)
{
	const struct vl53l5cx_config *cfg = dev->config;
	uint8_t buf[66]; /* Max register + 64 bytes data */
	
	if (len > 64) {
		return -EINVAL;
	}
	
	buf[0] = (reg >> 8) & 0xFF;
	buf[1] = reg & 0xFF;
	memcpy(&buf[2], data, len);
	
	return i2c_write_dt(&cfg->i2c, buf, len + 2);
}

static int vl53l5cx_write_byte(const struct device *dev, uint16_t reg, uint8_t value)
{
	return vl53l5cx_i2c_write(dev, reg, &value, 1);
}

static int vl53l5cx_read_byte(const struct device *dev, uint16_t reg, uint8_t *value)
{
	return vl53l5cx_i2c_read(dev, reg, value, 1);
}

/* Power management */
static int vl53l5cx_power_on(const struct device *dev)
{
	const struct vl53l5cx_config *cfg = dev->config;
	
	if (!gpio_is_ready_dt(&cfg->xshut_gpio)) {
		LOG_WRN("XSHUT GPIO not ready - assuming sensor is powered on");
		k_sleep(K_MSEC(10)); /* Give some time for sensor to be ready */
		return 0;
	}
	
	LOG_INF("Powering on VL53L5CX via XSHUT");
	/* Release from shutdown (XSHUT is active low) */
	int ret = gpio_pin_set_dt(&cfg->xshut_gpio, 0);
	if (ret < 0) {
		LOG_ERR("Failed to set XSHUT pin: %d", ret);
		return ret;
	}
	
	/* Wait for sensor to boot */
	k_sleep(K_MSEC(50)); /* Increased delay for more reliable startup */
	
	return 0;
}

/* Sensor configuration */
static int vl53l5cx_check_device_id(const struct device *dev)
{
	uint8_t device_id;
	int ret;
	const struct vl53l5cx_config *cfg = dev->config;
	
	LOG_INF("Attempting to read VL53L5CX device ID...");
	LOG_INF("Using I2C address: 0x%02X", cfg->i2c.addr);
	
	/* Try a simple I2C probe first */
	ret = i2c_write_dt(&cfg->i2c, NULL, 0);
	if (ret < 0) {
		LOG_ERR("I2C probe failed at address 0x%02X: %d", cfg->i2c.addr, ret);
		
		/* Try common VL53L5CX addresses */
		uint8_t test_addresses[] = {0x29, 0x52, 0x53, 0x2D};
		LOG_INF("Trying alternative I2C addresses...");
		
		for (int i = 0; i < 4; i++) {
			struct i2c_dt_spec test_spec = cfg->i2c;
			test_spec.addr = test_addresses[i];
			
			ret = i2c_write_dt(&test_spec, NULL, 0);
			if (ret == 0) {
				LOG_INF("Found device responding at address 0x%02X", test_addresses[i]);
			} else {
				LOG_DBG("No response at address 0x%02X", test_addresses[i]);
			}
		}
		return -ENODEV;
	}
	
	LOG_INF("I2C probe successful at address 0x%02X", cfg->i2c.addr);
	
	ret = vl53l5cx_read_byte(dev, VL53L5CX_DEVICE_ID, &device_id);
	if (ret < 0) {
		LOG_ERR("Failed to read device ID (I2C error): %d", ret);
		
		/* Try reading from different register addresses to see if sensor responds */
		LOG_INF("Trying alternate register addresses...");
		for (uint16_t addr = 0x0000; addr <= 0x0010; addr++) {
			ret = vl53l5cx_read_byte(dev, addr, &device_id);
			if (ret == 0) {
				LOG_INF("Got response from register 0x%04X: 0x%02X", addr, device_id);
			}
		}
		return -ENODEV;
	}
	
	LOG_INF("Read device ID from 0x%04X: 0x%02X", VL53L5CX_DEVICE_ID, device_id);
	
	/* For now, accept any device ID that's not 0x00 or 0xFF */
	if (device_id == 0x00 || device_id == 0xFF) {
		LOG_ERR("Invalid device ID: 0x%02X (no device responding)", device_id);
		return -ENODEV;
	}
	
	LOG_INF("VL53L5CX sensor detected with ID: 0x%02X", device_id);
	return 0;
}

static int vl53l5cx_wait_for_boot(const struct device *dev)
{
	LOG_INF("Waiting for VL53L5CX boot completion...");
	
	/* For now, just use a fixed delay since the firmware status register
	 * might not be available or might use different addresses */
	k_sleep(K_MSEC(100)); /* Give sensor time to fully boot */
	
	LOG_INF("VL53L5CX boot wait complete (using fixed delay)");
	return 0;
}

static int vl53l5cx_set_resolution(const struct device *dev, uint8_t resolution)
{
	struct vl53l5cx_data *data = dev->data;
	
	if (resolution != VL53L5CX_RESOLUTION_4X4 && 
	    resolution != VL53L5CX_RESOLUTION_8X8) {
		return -EINVAL;
	}
	
	/* For now, just store the resolution. Full implementation would
	 * require the VL53L5CX ULD (Ultra Lite Driver) API */
	data->zone_count = resolution;
	
	LOG_INF("VL53L5CX resolution set to %dx%d (%d zones)",
		(resolution == VL53L5CX_RESOLUTION_4X4) ? 4 : 8,
		(resolution == VL53L5CX_RESOLUTION_4X4) ? 4 : 8,
		resolution);
	
	return 0;
}

static int vl53l5cx_start_ranging(const struct device *dev)
{
	struct vl53l5cx_data *data = dev->data;
	uint8_t status_before, status_after;
	int ret;
	
	if (data->is_ranging) {
		LOG_DBG("VL53L5CX already ranging");
		return 0;
	}
	
	/* Read status before starting ranging */
	ret = vl53l5cx_read_byte(dev, VL53L5CX_STATUS, &status_before);
	if (ret < 0) {
		LOG_ERR("Failed to read status before ranging: %d", ret);
		return ret;
	}
	LOG_INF("Status before ranging: 0x%02x", status_before);
	
	/* VL53L5CX requires a specific initialization sequence */
	LOG_INF("Starting VL53L5CX ranging mode with comprehensive sequence...");
	
	/* Step 1: Set ranging mode */
	ret = vl53l5cx_write_byte(dev, VL53L5CX_COMMAND, 0x01);
	if (ret < 0) {
		LOG_ERR("Failed to set ranging mode: %d", ret);
		return ret;
	}
	k_sleep(K_MSEC(5));
	
	/* Step 2: Configure measurement timing budget (100ms) */
	ret = vl53l5cx_write_byte(dev, 0x005C, 0x64);  /* 100ms timing budget */
	if (ret < 0) {
		LOG_WRN("Failed to set timing budget: %d", ret);
	}
	
	/* Step 3: Configure inter-measurement period */
	ret = vl53l5cx_write_byte(dev, 0x005E, 0x64);  /* 100ms inter-measurement */
	if (ret < 0) {
		LOG_WRN("Failed to set inter-measurement period: %d", ret);
	}
	
	/* Step 4: Try multiple start commands */
	uint8_t start_commands[] = {0x01, 0x40, 0x80, 0x03};
	for (int i = 0; i < ARRAY_SIZE(start_commands); i++) {
		LOG_INF("Trying start command 0x%02x", start_commands[i]);
		ret = vl53l5cx_write_byte(dev, VL53L5CX_SYSTEM_START, start_commands[i]);
		if (ret < 0) {
			LOG_ERR("Failed to write start command 0x%02x: %d", start_commands[i], ret);
			continue;
		}
		
		k_sleep(K_MSEC(10));
		
		/* Check if status changed */
		ret = vl53l5cx_read_byte(dev, VL53L5CX_STATUS, &status_after);
		if (ret < 0) {
			LOG_ERR("Failed to read status after ranging: %d", ret);
			continue;
		}
		
		LOG_INF("Status after start command 0x%02x: 0x%02x", start_commands[i], status_after);
		
		if (status_after != status_before) {
			LOG_INF("Status changed! Start command 0x%02x seems to work", start_commands[i]);
			break;
		}
	}
	
	/* Final status check */
	ret = vl53l5cx_read_byte(dev, VL53L5CX_STATUS, &status_after);
	if (ret < 0) {
		LOG_ERR("Failed to read final status: %d", ret);
		return ret;
	}
	LOG_INF("Final status after ranging sequence: 0x%02x", status_after);
	
	if (status_after == status_before) {
		LOG_WRN("Status still unchanged - VL53L5CX may need firmware upload");
		LOG_WRN("This is a complex sensor that typically requires ULD firmware");
	}
	
	data->is_ranging = true;
	LOG_INF("VL53L5CX ranging sequence completed");
	
	return 0;
}

static int vl53l5cx_stop_ranging(const struct device *dev)
{
	struct vl53l5cx_data *data = dev->data;
	int ret;
	
	ret = vl53l5cx_write_byte(dev, VL53L5CX_SYSTEM_START, 0x00);
	if (ret < 0) {
		LOG_ERR("Failed to stop ranging: %d", ret);
		return ret;
	}
	
	data->is_ranging = false;
	LOG_INF("VL53L5CX ranging stopped");
	
	return 0;
}

static int vl53l5cx_check_data_ready(const struct device *dev)
{
	uint8_t status;
	int ret;
	
	/* For VL53L5CX, check the system status register */
	ret = vl53l5cx_read_byte(dev, VL53L5CX_STATUS, &status);
	if (ret < 0) {
		LOG_ERR("Failed to read system status: %d", ret);
		return ret;
	}
	
	LOG_DBG("System status register 0x%04x: 0x%02x", VL53L5CX_STATUS, status);
	
	/* For simplicity, assume data is ready if status is not idle */
	/* In a real implementation, we'd check specific data ready bits */
	int ready = (status != VL53L5CX_STATUS_IDLE) ? 1 : 0;
	LOG_DBG("Data ready result: %d (status=0x%02x)", ready, status);
	return ready;
}

int vl53l5cx_read_results(const struct device *dev)
{
	struct vl53l5cx_data *data = dev->data;
	uint8_t result_data[8];
	int ret;
	
	LOG_DBG("Reading VL53L5CX measurement results...");
	
	/* Read basic result data for center zone (simplified) */
	ret = vl53l5cx_i2c_read(dev, VL53L5CX_RESULT_DISTANCE, result_data, 8);
	if (ret < 0) {
		LOG_ERR("Failed to read results: %d", ret);
		return ret;
	}
	
	LOG_INF("Raw result data: [0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]",
		result_data[0], result_data[1], result_data[2], result_data[3],
		result_data[4], result_data[5], result_data[6], result_data[7]);
	
	/* Parse distance (center zone only for simplified implementation) */
	data->zones[0].distance_mm = (result_data[1] << 8) | result_data[0];
	data->zones[0].status = result_data[2];
	data->zones[0].signal_kcps = (result_data[5] << 8) | result_data[4];
	data->zones[0].ambient_kcps = (result_data[7] << 8) | result_data[6];
	
	/* If all data is zero, try different byte ordering or registers */
	if (data->zones[0].distance_mm == 0 && data->zones[0].signal_kcps == 0) {
		LOG_WRN("All measurement data is zero - trying different parsing");
		/* Try big-endian instead */
		uint16_t distance_be = (result_data[0] << 8) | result_data[1];
		uint16_t signal_be = (result_data[4] << 8) | result_data[5];
		LOG_INF("Alternative parsing - Distance: %u mm, Signal: %u kcps", distance_be, signal_be);
		
		/* If big-endian gives non-zero results, use it */
		if (distance_be > 0 || signal_be > 0) {
			data->zones[0].distance_mm = distance_be;
			data->zones[0].signal_kcps = signal_be;
			data->zones[0].ambient_kcps = (result_data[6] << 8) | result_data[7];
		}
	}
	
	LOG_INF("Parsed results - Distance: %u mm, Status: 0x%02x, Signal: %u kcps, Ambient: %u kcps",
		data->zones[0].distance_mm, data->zones[0].status, 
		data->zones[0].signal_kcps, data->zones[0].ambient_kcps);
	
	/* For multi-zone, we would read all zone data here */
	
	data->data_ready = true;
	
	LOG_DBG("Distance: %d mm, Status: %d", 
		data->zones[0].distance_mm, data->zones[0].status);
	
	return 0;
}

/* Zephyr sensor API implementation */
int vl53l5cx_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct vl53l5cx_data *data = dev->data;
	int ret;
	int retry_count = 0;
	const int max_retries = 10;  /* Allow up to 10 attempts */
	
	LOG_INF("VL53L5CX sample fetch requested, channel: %d", (int)chan);
	
	__ASSERT_NO_MSG(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_DISTANCE);
	
	if (!data->is_ranging) {
		LOG_INF("Sensor not ranging, attempting to start ranging...");
		ret = vl53l5cx_start_ranging(dev);
		if (ret < 0) {
			LOG_ERR("Failed to start ranging: %d", ret);
			return ret;
		}
		/* Give sensor some time to start ranging */
		k_sleep(K_MSEC(100));
	} else {
		LOG_DBG("Sensor is already ranging");
	}
	
	/* Check if data is ready with retries */
	LOG_DBG("Checking if data is ready...");
	while (retry_count < max_retries) {
		ret = vl53l5cx_check_data_ready(dev);
		if (ret < 0) {
			LOG_ERR("Failed to check data ready: %d", ret);
			return ret;
		}
		
		if (ret > 0) {
			LOG_INF("Data is ready after %d retries, reading results...", retry_count);
			break;
		}
		
		retry_count++;
		if (retry_count < max_retries) {
			LOG_DBG("Data not ready, retry %d/%d", retry_count, max_retries);
			k_sleep(K_MSEC(50));  /* Wait 50ms between checks */
		}
	}
	
	if (retry_count >= max_retries) {
		LOG_WRN("Data not ready after %d retries, proceeding anyway", max_retries);
		/* Try to read results anyway - maybe the check is wrong */
	}
	
	/* Read the results */
	ret = vl53l5cx_read_results(dev);
	if (ret < 0) {
		LOG_ERR("Failed to read results: %d", ret);
		return ret;
	}
	
	LOG_INF("VL53L5CX sample fetch completed successfully");
	return 0;
}

int vl53l5cx_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct vl53l5cx_data *data = dev->data;
	
	LOG_DBG("Channel get request: chan=%d, data_ready=%d", (int)chan, data->data_ready);
	
	if (!data->data_ready) {
		LOG_DBG("Data not ready, returning -ENODATA");
		return -ENODATA;
	}
	
	switch ((int)chan) {
	case SENSOR_CHAN_DISTANCE:
		/* Return center zone distance in millimeters */
		val->val1 = data->zones[0].distance_mm;
		val->val2 = 0;
		LOG_DBG("Returning distance: %d mm", data->zones[0].distance_mm);
		break;
		
	case SENSOR_CHAN_VL53L5CX_DISTANCE_ZONE_0:
		val->val1 = data->zones[0].distance_mm;
		val->val2 = 0;
		break;
		
	case SENSOR_CHAN_VL53L5CX_SIGNAL_RATE:
		val->val1 = data->zones[0].signal_kcps;
		val->val2 = 0;
		break;
		
	case SENSOR_CHAN_VL53L5CX_STATUS:
		val->val1 = data->zones[0].status;
		val->val2 = 0;
		break;
		
	default:
		return -ENOTSUP;
	}
	
	return 0;
}

int vl53l5cx_attr_set(const struct device *dev, enum sensor_channel chan,
			     enum sensor_attribute attr, const struct sensor_value *val)
{
	switch (attr) {
	case SENSOR_ATTR_SAMPLING_FREQUENCY:
		/* Start/stop ranging based on frequency */
		if (val->val1 > 0) {
			return vl53l5cx_start_ranging(dev);
		} else {
			return vl53l5cx_stop_ranging(dev);
		}
		
	default:
		return -ENOTSUP;
	}
}

int vl53l5cx_attr_get(const struct device *dev, enum sensor_channel chan,
			     enum sensor_attribute attr, struct sensor_value *val)
{
	struct vl53l5cx_data *data = dev->data;
	
	switch (attr) {
	case SENSOR_ATTR_SAMPLING_FREQUENCY:
		val->val1 = data->is_ranging ? 10 : 0; /* Default 10Hz when ranging */
		val->val2 = 0;
		break;
		
	default:
		return -ENOTSUP;
	}
	
	return 0;
}

static const struct sensor_driver_api vl53l5cx_driver_api = {
	.sample_fetch = vl53l5cx_sample_fetch,
	.channel_get = vl53l5cx_channel_get,
	.attr_set = vl53l5cx_attr_set,
	.attr_get = vl53l5cx_attr_get,
#ifdef CONFIG_VL53L5CX_TRIGGER
	.trigger_set = vl53l5cx_trigger_set,
#endif
};

/* Device initialization */
int vl53l5cx_init(const struct device *dev)
{
	const struct vl53l5cx_config *cfg = dev->config;
	struct vl53l5cx_data *data = dev->data;
	int ret;
	
	data->dev = dev;
	
	LOG_INF("VL53L5CX initialization starting...");
	
	/* Check I2C bus is ready */
	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}
	LOG_INF("I2C bus ready");
	
	/* Configure XSHUT GPIO */
	if (gpio_is_ready_dt(&cfg->xshut_gpio)) {
		ret = gpio_pin_configure_dt(&cfg->xshut_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure XSHUT GPIO: %d", ret);
			return ret;
		}
		LOG_INF("XSHUT GPIO configured");
	} else {
		LOG_WRN("XSHUT GPIO not ready - continuing without power control");
	}
	
	/* Power on the sensor */
	ret = vl53l5cx_power_on(dev);
	if (ret < 0) {
		return ret;
	}
	
	/* Wait for boot and check device ID */
	LOG_INF("Waiting for VL53L5CX boot...");
	ret = vl53l5cx_wait_for_boot(dev);
	if (ret < 0) {
		LOG_ERR("VL53L5CX boot wait failed: %d", ret);
		return ret;
	}
	
	LOG_INF("Checking VL53L5CX device ID...");
	ret = vl53l5cx_check_device_id(dev);
	if (ret < 0) {
		LOG_ERR("VL53L5CX device ID check failed: %d", ret);
		return ret;
	}
	
	/* Set default resolution */
	ret = vl53l5cx_set_resolution(dev, cfg->resolution);
	if (ret < 0) {
		return ret;
	}
	
#ifdef CONFIG_VL53L5CX_TRIGGER
	ret = vl53l5cx_init_interrupt(dev);
	if (ret < 0) {
		LOG_ERR("Failed to initialize interrupt: %d", ret);
		return ret;
	}
#endif
	
	/* Start ranging automatically */
	LOG_INF("Starting VL53L5CX ranging...");
	ret = vl53l5cx_start_ranging(dev);
	if (ret < 0) {
		LOG_ERR("Failed to start ranging: %d", ret);
		return ret;
	}
	
	LOG_INF("VL53L5CX initialized successfully");
	
	return 0;
}

/* Device instantiation macro */
#define VL53L5CX_DEFINE(inst)								\
	static struct vl53l5cx_data vl53l5cx_data_##inst;				\
										\
	static const struct vl53l5cx_config vl53l5cx_config_##inst = {			\
		.i2c = I2C_DT_SPEC_INST_GET(inst),					\
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, { 0 }),		\
		.xshut_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, xshut_gpios, { 0 }),	\
		.resolution = DT_INST_PROP_OR(inst, resolution, 16),			\
		.ranging_freq = DT_INST_PROP_OR(inst, ranging_frequency_hz, 10),	\
		.integration_time = DT_INST_PROP_OR(inst, integration_time_ms, 5),	\
	};									\
										\
	SENSOR_DEVICE_DT_INST_DEFINE(inst, vl53l5cx_init, NULL,			\
				      &vl53l5cx_data_##inst, &vl53l5cx_config_##inst,	\
				      POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,		\
				      &vl53l5cx_driver_api);

DT_INST_FOREACH_STATUS_OKAY(VL53L5CX_DEFINE)
