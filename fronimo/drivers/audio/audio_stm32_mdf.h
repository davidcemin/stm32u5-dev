/*
 * Copyright (c) 2025 David Cemin
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_AUDIO_STM32_MDF_H_
#define ZEPHYR_DRIVERS_AUDIO_STM32_MDF_H_

#include <zephyr/device.h>
#include <zephyr/audio/dmic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STM32 MDF (Multithreaded Digital Filter) driver API
 * 
 * This driver provides support for STM32U5 MDF peripheral which is used
 * for PDM microphone interface with hardware digital filtering.
 */

/* MDF specific configuration structures */
struct stm32_mdf_cfg {
	uint32_t sample_rate;        /**< Target sample rate in Hz */
	uint8_t channels;            /**< Number of active channels */
	uint8_t decimation_ratio;    /**< Decimation ratio (4-512) */
	uint8_t filter_mode;         /**< Filter mode (LPF, HPF, BPF) */
	uint16_t gain;               /**< Digital gain */
	bool sound_detection;        /**< Enable sound detection */
	uint32_t threshold;          /**< Sound detection threshold */
};

/* MDF events */
enum stm32_mdf_event {
	STM32_MDF_EVT_DATA_READY = 0,
	STM32_MDF_EVT_OVERFLOW,
	STM32_MDF_EVT_UNDERRUN,
	STM32_MDF_EVT_SOUND_DETECTED,
	STM32_MDF_EVT_ERROR
};

/* Callback function type */
typedef void (*stm32_mdf_callback_t)(const struct device *dev, 
				      void *user_data,
				      enum stm32_mdf_event event,
				      int32_t *data, size_t size);

/**
 * @brief Configure MDF-specific parameters
 * 
 * @param dev MDF device
 * @param cfg MDF configuration
 * @return 0 on success, negative errno on error
 */
int stm32_mdf_configure_extended(const struct device *dev, 
				 const struct stm32_mdf_cfg *cfg);

/**
 * @brief Set callback for MDF events
 * 
 * @param dev MDF device
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on error
 */
int stm32_mdf_set_callback(const struct device *dev,
			    stm32_mdf_callback_t callback,
			    void *user_data);

/**
 * @brief Get current filter status
 * 
 * @param dev MDF device
 * @param status Pointer to store status flags
 * @return 0 on success, negative errno on error
 */
int stm32_mdf_get_status(const struct device *dev, uint32_t *status);

/**
 * @brief Configure sound detection
 * 
 * @param dev MDF device
 * @param enable Enable/disable sound detection
 * @param threshold Detection threshold
 * @return 0 on success, negative errno on error
 */
int stm32_mdf_configure_sound_detection(const struct device *dev,
					 bool enable, uint32_t threshold);

/**
 * @brief Read raw filter data (bypassing ring buffer)
 * 
 * @param dev MDF device
 * @param data Buffer to store data
 * @param size Number of samples to read
 * @return Number of samples read, negative errno on error
 */
int stm32_mdf_read_raw(const struct device *dev, int32_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_AUDIO_STM32_MDF_H_ */
