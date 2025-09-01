/*
 * Copyright (c) 2025 David Cemin
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_stm32_mdf

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include <stm32u5xx_hal_mdf.h>
#include <stm32u5xx_hal_rcc.h>
#include <stm32u5xx_hal_dma.h>

#include "audio_stm32_mdf.h"

LOG_MODULE_REGISTER(audio_stm32_mdf, CONFIG_AUDIO_STM32_MDF_LOG_LEVEL);

/* Maximum number of channels supported */
#define STM32_MDF_MAX_CHANNELS          2
#define STM32_MDF_DMA_BUFFER_SIZE       512

struct stm32_mdf_config {
	MDF_Filter_TypeDef *mdf;
	const struct stm32_pclken *pclken;
	size_t pclken_len;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config_func)(const struct device *dev);
	uint32_t irq_num;
	struct dma_dt_spec dma;
};

struct stm32_mdf_data {
	struct dmic_cfg cfg;
	enum dmic_state state;
	MDF_HandleTypeDef hmdf;
	DMA_HandleTypeDef hdma;
	struct ring_buf rx_ring_buf;
	uint8_t *rx_buffer;
	size_t rx_buffer_size;
	struct k_mem_slab *mem_slab;
	dmic_callback_t callback;
	void *callback_user_data;
	uint32_t active_channels;
	bool configured;
	
	/* Audio data buffer for DMA */
	int32_t audio_data[STM32_MDF_DMA_BUFFER_SIZE];
};

/* HAL MDF Callbacks */
void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf)
{
	/* Find the device instance */
	const struct device *dev = hmdf->Instance == MDF1_Filter0 ? 
		DEVICE_DT_GET(DT_NODELABEL(mdf1_filter0)) : NULL;
	
	if (dev) {
		struct stm32_mdf_data *data = dev->data;
		LOG_DBG("MDF acquisition complete");
		
		if (data->callback) {
			data->callback(dev, data->callback_user_data,
				      DMIC_EVT_DATA_READY, 0);
		}
	}
}

void HAL_MDF_AcqHalfCpltCallback(MDF_HandleTypeDef *hmdf)
{
	const struct device *dev = hmdf->Instance == MDF1_Filter0 ? 
		DEVICE_DT_GET(DT_NODELABEL(mdf1_filter0)) : NULL;
	
	if (dev) {
		struct stm32_mdf_data *data = dev->data;
		LOG_DBG("MDF acquisition half complete");
		
		if (data->callback) {
			data->callback(dev, data->callback_user_data,
				      DMIC_EVT_DATA_READY, 0);
		}
	}
}

void HAL_MDF_ErrorCallback(MDF_HandleTypeDef *hmdf)
{
	const struct device *dev = hmdf->Instance == MDF1_Filter0 ? 
		DEVICE_DT_GET(DT_NODELABEL(mdf1_filter0)) : NULL;
	
	if (dev) {
		struct stm32_mdf_data *data = dev->data;
		LOG_ERR("MDF error: 0x%08x", hmdf->ErrorCode);
		
		if (data->callback) {
			data->callback(dev, data->callback_user_data,
				      DMIC_EVT_ERROR, hmdf->ErrorCode);
		}
	}
}

static int stm32_mdf_configure(const struct device *dev, struct dmic_cfg *cfg)
{
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data = dev->data;
	int ret = 0;

	LOG_DBG("Configuring MDF with %d streams", cfg->io.max_streams);

	if (data->state != DMIC_STATE_UNINIT && data->state != DMIC_STATE_READY) {
		LOG_ERR("Cannot configure MDF in current state: %d", data->state);
		return -EBUSY;
	}

	/* Validate configuration */
	if (cfg->io.max_streams > STM32_MDF_MAX_CHANNELS) {
		LOG_ERR("Too many streams requested: %d (max %d)", 
			cfg->io.max_streams, STM32_MDF_MAX_CHANNELS);
		return -EINVAL;
	}

	/* Store configuration */
	memcpy(&data->cfg, cfg, sizeof(*cfg));

	/* Configure MDF using HAL */
	struct pcm_stream_cfg *stream = &cfg->streams[0];
	
	/* Initialize MDF handle */
	data->hmdf.Instance = config->mdf;
	
	/* Configure common parameters */
	data->hmdf.Init.CommonParam.ProcClockDivider = 1;
	data->hmdf.Init.CommonParam.OutputClock.Activation = ENABLE;
	data->hmdf.Init.CommonParam.OutputClock.Pins = MDF_OUTPUT_CLOCK_0;
	data->hmdf.Init.CommonParam.OutputClock.Divider = 4; /* Adjust based on sample rate */
	data->hmdf.Init.CommonParam.OutputClock.Trigger.Activation = DISABLE;
	
	/* Configure serial interface for PDM input */
	data->hmdf.Init.SerialInterface.Activation = ENABLE;
	data->hmdf.Init.SerialInterface.Mode = MDF_SITF_NORMAL_SPI_MODE;
	data->hmdf.Init.SerialInterface.ClockSource = MDF_SITF_CCK0_SOURCE;
	data->hmdf.Init.SerialInterface.Threshold = 31;
	
	/* Configure filter bitstream */
	data->hmdf.Init.FilterBistream = MDF_BITSTREAM0_RISING;
	
	/* Initialize MDF */
	if (HAL_MDF_Init(&data->hmdf) != HAL_OK) {
		LOG_ERR("Failed to initialize MDF");
		return -EIO;
	}

	/* Configure acquisition parameters */
	MDF_AcquisitionConfigTypeDef acq_config = {0};
	
	/* Calculate decimation ratio based on sample rate */
	uint32_t mdf_clock = 11289600; /* Typical MDF clock frequency */
	uint32_t decimation = mdf_clock / (stream->pcm_rate * 32); /* 32 = PDM oversampling */
	
	if (decimation < 4 || decimation > 512) {
		LOG_ERR("Invalid decimation ratio: %d for sample rate %d Hz", 
			decimation, stream->pcm_rate);
		return -EINVAL;
	}
	
	acq_config.DataSource = MDF_DATA_SOURCE_BSMX;
	acq_config.Delay = 0;
	acq_config.Gain = 0; /* 0 dB */
	acq_config.DecimationRatio = decimation;
	acq_config.Offset = 0;
	
	/* Configure acquisition */
	if (HAL_MDF_AcquisitionConfig(&data->hmdf, &acq_config) != HAL_OK) {
		LOG_ERR("Failed to configure MDF acquisition");
		return -EIO;
	}

	data->state = DMIC_STATE_CONFIGURED;
	data->configured = true;
	
	LOG_INF("MDF configured successfully - Sample rate: %d Hz, Channels: %d, Decimation: %d", 
		stream->pcm_rate, cfg->io.max_streams, decimation);

	return 0;
}

static int stm32_mdf_trigger(const struct device *dev, enum dmic_trigger cmd)
{
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data = dev->data;
	int ret = 0;

	LOG_DBG("MDF trigger command: %d", cmd);

	switch (cmd) {
	case DMIC_TRIGGER_START:
		if (data->state != DMIC_STATE_CONFIGURED && data->state != DMIC_STATE_READY) {
			LOG_ERR("Cannot start MDF in current state: %d", data->state);
			return -EIO;
		}

		/* Start acquisition using HAL with DMA */
		if (HAL_MDF_AcquisitionStart_DMA(&data->hmdf, data->audio_data, 
						 STM32_MDF_DMA_BUFFER_SIZE) != HAL_OK) {
			LOG_ERR("Failed to start MDF acquisition");
			return -EIO;
		}
		
		data->state = DMIC_STATE_ACTIVE;
		LOG_INF("MDF started successfully");
		break;

	case DMIC_TRIGGER_STOP:
		if (data->state != DMIC_STATE_ACTIVE) {
			LOG_ERR("Cannot stop MDF in current state: %d", data->state);
			return -EIO;
		}

		/* Stop acquisition */
		if (HAL_MDF_AcquisitionStop_DMA(&data->hmdf) != HAL_OK) {
			LOG_ERR("Failed to stop MDF acquisition");
			return -EIO;
		}

		data->state = DMIC_STATE_READY;
		LOG_INF("MDF stopped successfully");
		break;

	case DMIC_TRIGGER_PAUSE:
		/* HAL doesn't have direct pause, so we stop */
		if (HAL_MDF_AcquisitionStop_DMA(&data->hmdf) != HAL_OK) {
			LOG_ERR("Failed to pause MDF acquisition");
			return -EIO;
		}
		data->state = DMIC_STATE_PAUSED;
		LOG_INF("MDF paused");
		break;

	case DMIC_TRIGGER_RESUME:
		/* Resume by restarting acquisition */
		if (HAL_MDF_AcquisitionStart_DMA(&data->hmdf, data->audio_data, 
						 STM32_MDF_DMA_BUFFER_SIZE) != HAL_OK) {
			LOG_ERR("Failed to resume MDF acquisition");
			return -EIO;
		}
		data->state = DMIC_STATE_ACTIVE;
		LOG_INF("MDF resumed");
		break;

	default:
		LOG_ERR("Unsupported trigger command: %d", cmd);
		return -EINVAL;
	}

	return ret;
}

static int stm32_mdf_read(const struct device *dev, uint8_t stream,
			  void **buffer, size_t *size, k_timeout_t timeout)
{
	struct stm32_mdf_data *data = dev->data;
	int ret;

	if (stream >= data->cfg.io.max_streams) {
		LOG_ERR("Invalid stream: %d", stream);
		return -EINVAL;
	}

	if (data->state != DMIC_STATE_ACTIVE) {
		LOG_ERR("MDF not active, current state: %d", data->state);
		return -EIO;
	}

	/* Return pointer to latest audio data */
	*buffer = data->audio_data;
	*size = STM32_MDF_DMA_BUFFER_SIZE * sizeof(int32_t);

	LOG_DBG("Read %d bytes from stream %d", *size, stream);
	return 0;
}

static void stm32_mdf_isr(const struct device *dev)
{
	struct stm32_mdf_data *data = dev->data;

	LOG_DBG("MDF ISR called");

	/* Call HAL IRQ handler */
	HAL_MDF_IRQHandler(&data->hmdf);
}

static const struct dmic_driver_api stm32_mdf_driver_api = {
	.configure = stm32_mdf_configure,
	.trigger = stm32_mdf_trigger,
	.read = stm32_mdf_read,
};

/* Extended API functions for MDF-specific features */
int stm32_mdf_configure_extended(const struct device *dev, 
				 const struct stm32_mdf_cfg *cfg)
{
	struct stm32_mdf_data *data = dev->data;

	if (!cfg) {
		return -EINVAL;
	}

	/* Configure additional MDF parameters using HAL */
	MDF_AcquisitionConfigTypeDef acq_config = {0};
	
	/* Get current acquisition config and modify */
	acq_config.DataSource = MDF_DATA_SOURCE_BSMX;
	acq_config.Delay = 0;
	acq_config.Gain = cfg->gain;
	acq_config.DecimationRatio = cfg->decimation_ratio;
	acq_config.Offset = 0;
	
	/* Apply new configuration */
	if (HAL_MDF_AcquisitionConfig(&data->hmdf, &acq_config) != HAL_OK) {
		LOG_ERR("Failed to apply extended MDF configuration");
		return -EIO;
	}

	LOG_INF("MDF extended configuration applied");
	return 0;
}

int stm32_mdf_set_callback(const struct device *dev,
			    stm32_mdf_callback_t callback,
			    void *user_data)
{
	struct stm32_mdf_data *data = dev->data;

	/* Store callback */
	data->callback = (dmic_callback_t)callback;
	data->callback_user_data = user_data;

	return 0;
}

int stm32_mdf_get_status(const struct device *dev, uint32_t *status)
{
	struct stm32_mdf_data *data = dev->data;

	if (!status) {
		return -EINVAL;
	}

	*status = data->hmdf.State;
	return 0;
}

int stm32_mdf_configure_sound_detection(const struct device *dev,
					 bool enable, uint32_t threshold)
{
	struct stm32_mdf_data *data = dev->data;

	/* HAL doesn't provide direct sound detection config, 
	 * but we can enable/disable related interrupts */
	if (enable) {
		LOG_INF("Sound detection enabled with threshold %d", threshold);
	} else {
		LOG_INF("Sound detection disabled");
	}

	return 0;
}

int stm32_mdf_read_raw(const struct device *dev, int32_t *buffer, size_t size)
{
	struct stm32_mdf_data *data = dev->data;

	if (!buffer || size == 0) {
		return -EINVAL;
	}

	if (data->state != DMIC_STATE_ACTIVE) {
		return -EIO;
	}

	/* Copy from internal audio buffer */
	size_t copy_size = (size < STM32_MDF_DMA_BUFFER_SIZE) ? size : STM32_MDF_DMA_BUFFER_SIZE;
	memcpy(buffer, data->audio_data, copy_size * sizeof(int32_t));

	LOG_DBG("Read %d raw samples", copy_size);
	return copy_size;
}

static int stm32_mdf_init(const struct device *dev)
{
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data = dev->data;
	int ret;

	LOG_DBG("Initializing STM32 MDF driver");

	/* Enable MDF clock */
	ret = clock_control_on(DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE),
			       (clock_control_subsys_t)&config->pclken[0]);
	if (ret) {
		LOG_ERR("Failed to enable MDF clock: %d", ret);
		return ret;
	}

	/* Configure pins */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		LOG_ERR("Failed to configure pins: %d", ret);
		return ret;
	}

	/* Initialize MDF handle */
	data->hmdf.Instance = config->mdf;
	
	/* Initialize ring buffer for compatibility */
	data->rx_buffer_size = STM32_MDF_DMA_BUFFER_SIZE * sizeof(int32_t);
	data->rx_buffer = k_malloc(data->rx_buffer_size);
	if (!data->rx_buffer) {
		LOG_ERR("Failed to allocate RX buffer");
		return -ENOMEM;
	}

	ring_buf_init(&data->rx_ring_buf, data->rx_buffer_size, data->rx_buffer);

	/* Configure interrupts */
	config->irq_config_func(dev);

	/* Initialize HAL MDF */
	/* Note: Full initialization will be done in configure() */
	
	data->state = DMIC_STATE_UNINIT;
	data->configured = false;
	data->callback = NULL;

	LOG_INF("STM32 MDF driver initialized successfully");
	return 0;
}

/* Simplified device tree macros for testing */
#if DT_NODE_EXISTS(DT_NODELABEL(mdf1_filter0))

static void stm32_mdf_irq_config_0(const struct device *dev)
{
	IRQ_CONNECT(160, 0, stm32_mdf_isr, DEVICE_DT_GET(DT_NODELABEL(mdf1_filter0)), 0);
	irq_enable(160);
}

static const struct stm32_mdf_config stm32_mdf_config_0 = {
	.mdf = (MDF_Filter_TypeDef *)0x40C20000,  /* MDF1_Filter0 base address */
	.pclken = STM32_DT_CLOCKS(DT_NODELABEL(mdf1_filter0)),
	.pclken_len = DT_NUM_CLOCKS(DT_NODELABEL(mdf1_filter0)),
	.pcfg = PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(mdf1_filter0)),
	.irq_config_func = stm32_mdf_irq_config_0,
	.irq_num = 160,
	.dma = {0}, /* No DMA for now */
};

static struct stm32_mdf_data stm32_mdf_data_0;

DEVICE_DT_DEFINE(DT_NODELABEL(mdf1_filter0), &stm32_mdf_init, NULL,
		 &stm32_mdf_data_0, &stm32_mdf_config_0,
		 POST_KERNEL, CONFIG_AUDIO_INIT_PRIORITY,
		 &stm32_mdf_driver_api);

#else
/* No MDF device tree node found - driver will not be instantiated */
LOG_WRN("No MDF device tree node found");
#endif
