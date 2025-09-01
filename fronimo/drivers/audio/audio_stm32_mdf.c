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

#include <stm32_ll_mdf.h>
#include <stm32_ll_rcc.h>

LOG_MODULE_REGISTER(audio_stm32_mdf, CONFIG_AUDIO_STM32_MDF_LOG_LEVEL);

/* MDF register definitions for STM32U5 */
#define MDF_DFLTCR_DFLTEN               (1U << 0)
#define MDF_DFLTCR_DMAEN                (1U << 1)
#define MDF_DFLTCR_FLT_MODE             (3U << 2)
#define MDF_DFLTCR_FLT_MODE_LPF         (0U << 2)
#define MDF_DFLTCR_FLT_MODE_HPF         (1U << 2)
#define MDF_DFLTCR_FLT_MODE_BPF         (2U << 2)

#define MDF_DFLTIER_RFOVRIE             (1U << 0)
#define MDF_DFLTIER_SDDETIE             (1U << 1)
#define MDF_DFLTIER_RFURIE              (1U << 2)

#define MDF_DFLTISR_RFOVRF              (1U << 0)
#define MDF_DFLTISR_SDDETF              (1U << 1)
#define MDF_DFLTISR_RFURF               (1U << 2)

/* Maximum number of channels supported */
#define STM32_MDF_MAX_CHANNELS          2

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
	struct ring_buf rx_ring_buf;
	uint8_t *rx_buffer;
	size_t rx_buffer_size;
	struct k_mem_slab *mem_slab;
	dmic_callback_t callback;
	void *callback_user_data;
	uint32_t active_channels;
	bool configured;
};

static int stm32_mdf_configure(const struct device *dev, struct dmic_cfg *cfg)
{
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data = dev->data;
	MDF_Filter_TypeDef *mdf = config->mdf;
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

	/* Configure MDF peripheral */
	struct pcm_stream_cfg *stream = &cfg->streams[0];
	
	/* Configure digital filter control register */
	uint32_t dfltcr = 0;
	
	/* Set decimation ratio based on sample rate */
	uint32_t mdf_clock = 11289600; /* Typical MDF clock frequency */
	uint32_t decimation = mdf_clock / (stream->pcm_rate * 64); /* 64 = oversampling */
	
	if (decimation < 4 || decimation > 512) {
		LOG_ERR("Invalid decimation ratio: %d", decimation);
		return -EINVAL;
	}

	/* Configure the filter */
	dfltcr |= MDF_DFLTCR_FLT_MODE_LPF; /* Low-pass filter mode */
	
	/* Write configuration to registers */
	mdf->DFLTCR = dfltcr;
	
	/* Configure gain and offset */
	mdf->DFLTGCR = 0; /* Unity gain initially */
	
	/* Configure interrupt mask */
	mdf->DFLTIER = MDF_DFLTIER_RFOVRIE | MDF_DFLTIER_SDDETIE;

	/* Setup DMA if configured */
	if (config->dma.dma_dev) {
		struct dma_config dma_cfg = {
			.channel_direction = PERIPHERAL_TO_MEMORY,
			.dma_callback = NULL, /* Will be set during trigger */
			.user_data = (void *)dev,
			.dma_slot = config->dma.channel,
		};
		
		ret = dma_config(config->dma.dma_dev, config->dma.channel, &dma_cfg);
		if (ret) {
			LOG_ERR("Failed to configure DMA: %d", ret);
			return ret;
		}
	}

	data->state = DMIC_STATE_CONFIGURED;
	data->configured = true;
	
	LOG_INF("MDF configured successfully - Sample rate: %d Hz, Channels: %d", 
		stream->pcm_rate, cfg->io.max_streams);

	return 0;
}

static int stm32_mdf_trigger(const struct device *dev, enum dmic_trigger cmd)
{
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data = dev->data;
	MDF_Filter_TypeDef *mdf = config->mdf;
	int ret = 0;

	LOG_DBG("MDF trigger command: %d", cmd);

	switch (cmd) {
	case DMIC_TRIGGER_START:
		if (data->state != DMIC_STATE_CONFIGURED && data->state != DMIC_STATE_READY) {
			LOG_ERR("Cannot start MDF in current state: %d", data->state);
			return -EIO;
		}

		/* Enable DMA */
		if (config->dma.dma_dev) {
			ret = dma_start(config->dma.dma_dev, config->dma.channel);
			if (ret) {
				LOG_ERR("Failed to start DMA: %d", ret);
				return ret;
			}
		}

		/* Enable MDF filter and DMA */
		mdf->DFLTCR |= MDF_DFLTCR_DFLTEN | MDF_DFLTCR_DMAEN;
		
		data->state = DMIC_STATE_ACTIVE;
		LOG_INF("MDF started successfully");
		break;

	case DMIC_TRIGGER_STOP:
		if (data->state != DMIC_STATE_ACTIVE) {
			LOG_ERR("Cannot stop MDF in current state: %d", data->state);
			return -EIO;
		}

		/* Disable MDF filter and DMA */
		mdf->DFLTCR &= ~(MDF_DFLTCR_DFLTEN | MDF_DFLTCR_DMAEN);

		/* Stop DMA */
		if (config->dma.dma_dev) {
			ret = dma_stop(config->dma.dma_dev, config->dma.channel);
			if (ret) {
				LOG_ERR("Failed to stop DMA: %d", ret);
			}
		}

		data->state = DMIC_STATE_READY;
		LOG_INF("MDF stopped successfully");
		break;

	case DMIC_TRIGGER_PAUSE:
		/* Disable MDF filter but keep DMA enabled */
		mdf->DFLTCR &= ~MDF_DFLTCR_DFLTEN;
		data->state = DMIC_STATE_PAUSED;
		LOG_INF("MDF paused");
		break;

	case DMIC_TRIGGER_RESUME:
		/* Re-enable MDF filter */
		mdf->DFLTCR |= MDF_DFLTCR_DFLTEN;
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

	/* Try to get data from ring buffer */
	uint32_t available = ring_buf_size_get(&data->rx_ring_buf);
	if (available == 0) {
		if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			return -EAGAIN;
		}
		/* TODO: Implement blocking read with timeout */
		return -ENODATA;
	}

	/* Read data from ring buffer */
	uint32_t read_size = ring_buf_get(&data->rx_ring_buf, *buffer, *size);
	*size = read_size;

	LOG_DBG("Read %d bytes from stream %d", read_size, stream);
	return 0;
}

static void stm32_mdf_isr(const struct device *dev)
{
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data = dev->data;
	MDF_Filter_TypeDef *mdf = config->mdf;
	uint32_t status = mdf->DFLTISR;

	LOG_DBG("MDF ISR: status=0x%08x", status);

	if (status & MDF_DFLTISR_RFOVRF) {
		LOG_WRN("MDF RX FIFO overflow");
		/* Clear overflow flag */
		mdf->DFLTICR = MDF_DFLTISR_RFOVRF;
		
		if (data->callback) {
			data->callback(dev, data->callback_user_data, 
				      DMIC_EVT_FIFO_OVERFLOW, 0);
		}
	}

	if (status & MDF_DFLTISR_SDDETF) {
		LOG_DBG("MDF sound detection");
		/* Clear flag */
		mdf->DFLTICR = MDF_DFLTISR_SDDETF;
		
		if (data->callback) {
			data->callback(dev, data->callback_user_data,
				      DMIC_EVT_DATA_READY, 0);
		}
	}

	if (status & MDF_DFLTISR_RFURF) {
		LOG_WRN("MDF RX FIFO underrun");
		/* Clear flag */
		mdf->DFLTICR = MDF_DFLTISR_RFURF;
	}
}

static void stm32_mdf_dma_callback(const struct device *dma_dev, void *user_data,
				   uint32_t channel, int status)
{
	const struct device *dev = (const struct device *)user_data;
	struct stm32_mdf_data *data = dev->data;

	if (status < 0) {
		LOG_ERR("DMA error: %d", status);
		if (data->callback) {
			data->callback(dev, data->callback_user_data,
				      DMIC_EVT_ERROR, status);
		}
		return;
	}

	LOG_DBG("DMA transfer complete");
	
	if (data->callback) {
		data->callback(dev, data->callback_user_data,
			      DMIC_EVT_DATA_READY, 0);
	}
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
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data = dev->data;
	MDF_Filter_TypeDef *mdf = config->mdf;

	if (!cfg) {
		return -EINVAL;
	}

	/* Configure decimation ratio */
	if (cfg->decimation_ratio < 4 || cfg->decimation_ratio > 512) {
		LOG_ERR("Invalid decimation ratio: %d", cfg->decimation_ratio);
		return -EINVAL;
	}

	/* Configure filter mode and gain */
	uint32_t dfltcr = mdf->DFLTCR;
	dfltcr &= ~MDF_DFLTCR_FLT_MODE;
	dfltcr |= (cfg->filter_mode & 0x3) << 2;
	mdf->DFLTCR = dfltcr;

	/* Configure gain */
	mdf->DFLTGCR = cfg->gain & 0xFFFF;

	/* Configure sound detection if enabled */
	if (cfg->sound_detection) {
		/* Enable sound detection interrupt */
		mdf->DFLTIER |= MDF_DFLTIER_SDDETIE;
		/* Set threshold - this would be implementation specific */
	}

	LOG_INF("MDF extended configuration applied");
	return 0;
}

int stm32_mdf_set_callback(const struct device *dev,
			    stm32_mdf_callback_t callback,
			    void *user_data)
{
	struct stm32_mdf_data *data = dev->data;

	/* Store callback - we'll use the generic dmic_callback_t for now */
	data->callback = (dmic_callback_t)callback;
	data->callback_user_data = user_data;

	return 0;
}

int stm32_mdf_get_status(const struct device *dev, uint32_t *status)
{
	const struct stm32_mdf_config *config = dev->config;
	MDF_Filter_TypeDef *mdf = config->mdf;

	if (!status) {
		return -EINVAL;
	}

	*status = mdf->DFLTISR;
	return 0;
}

int stm32_mdf_configure_sound_detection(const struct device *dev,
					 bool enable, uint32_t threshold)
{
	const struct stm32_mdf_config *config = dev->config;
	MDF_Filter_TypeDef *mdf = config->mdf;

	if (enable) {
		/* Enable sound detection interrupt */
		mdf->DFLTIER |= MDF_DFLTIER_SDDETIE;
		/* Configure threshold (implementation specific) */
		LOG_INF("Sound detection enabled with threshold %d", threshold);
	} else {
		/* Disable sound detection interrupt */
		mdf->DFLTIER &= ~MDF_DFLTIER_SDDETIE;
		LOG_INF("Sound detection disabled");
	}

	return 0;
}

int stm32_mdf_read_raw(const struct device *dev, int32_t *data, size_t size)
{
	const struct stm32_mdf_config *config = dev->config;
	struct stm32_mdf_data *data_ctx = dev->data;
	MDF_Filter_TypeDef *mdf = config->mdf;

	if (!data || size == 0) {
		return -EINVAL;
	}

	if (data_ctx->state != DMIC_STATE_ACTIVE) {
		return -EIO;
	}

	/* Read directly from MDF data register */
	size_t samples_read = 0;
	for (size_t i = 0; i < size && samples_read < size; i++) {
		/* Check if data is available */
		if (!(mdf->DFLTISR & 0x1)) { /* Check data ready flag */
			break;
		}
		
		/* Read sample from data register */
		data[i] = mdf->DFLTDR; /* MDF Data Register */
		samples_read++;
	}

	LOG_DBG("Read %d raw samples", samples_read);
	return samples_read;
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

	/* Initialize ring buffer for RX data */
	data->rx_buffer_size = CONFIG_AUDIO_STM32_MDF_DMA_BUFFER_SIZE;
	data->rx_buffer = k_malloc(data->rx_buffer_size);
	if (!data->rx_buffer) {
		LOG_ERR("Failed to allocate RX buffer");
		return -ENOMEM;
	}

	ring_buf_init(&data->rx_ring_buf, data->rx_buffer_size, data->rx_buffer);

	/* Configure interrupts */
	config->irq_config_func(dev);

	/* Reset MDF peripheral */
	MDF_Filter_TypeDef *mdf = config->mdf;
	mdf->DFLTCR = 0;
	mdf->DFLTIER = 0;
	mdf->DFLTICR = 0xFFFFFFFF; /* Clear all flags */

	data->state = DMIC_STATE_UNINIT;
	data->configured = false;
	data->callback = NULL;

	LOG_INF("STM32 MDF driver initialized successfully");
	return 0;
}

/* Device tree macros for STM32 MDF */
#define STM32_MDF_INIT(n)                                                     \
	PINCTRL_DT_INST_DEFINE(n);                                            \
                                                                               \
	static void stm32_mdf_irq_config_##n(const struct device *dev)        \
	{                                                                      \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority),        \
			    stm32_mdf_isr, DEVICE_DT_INST_GET(n), 0);         \
		irq_enable(DT_INST_IRQN(n));                                  \
	}                                                                      \
                                                                               \
	static const struct stm32_mdf_config stm32_mdf_config_##n = {         \
		.mdf = (MDF_Filter_TypeDef *)DT_INST_REG_ADDR(n),             \
		.pclken = STM32_DT_INST_CLOCKS(n),                            \
		.pclken_len = DT_INST_NUM_CLOCKS(n),                          \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                    \
		.irq_config_func = stm32_mdf_irq_config_##n,                  \
		.irq_num = DT_INST_IRQN(n),                                   \
		.dma = DMA_DT_SPEC_INST_GET_OR(n, {0}),                       \
	};                                                                     \
                                                                               \
	static struct stm32_mdf_data stm32_mdf_data_##n;                      \
                                                                               \
	DEVICE_DT_INST_DEFINE(n, &stm32_mdf_init, NULL,                       \
			       &stm32_mdf_data_##n, &stm32_mdf_config_##n,    \
			       POST_KERNEL, CONFIG_AUDIO_INIT_PRIORITY,        \
			       &stm32_mdf_driver_api);

DT_INST_FOREACH_STATUS_OKAY(STM32_MDF_INIT)
