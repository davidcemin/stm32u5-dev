#include "pdm_mics.h"
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/audio/dmic.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LOG_MODULE_REGISTER(pdm_mics_cpp, LOG_LEVEL_INF);

/* Device tree node for PDM microphones */
#define PDM_NODE DT_ALIAS(pdm_mic)
/* Try to get the MDF device if available */
/* TODO: Fix device tree symbol generation for MDF device */
#if 0  // Temporarily disabled until device instantiation is fixed
#define MDF_DEVICE DEVICE_DT_GET(DT_NODELABEL(mdf1_filter0))
#else
#define MDF_DEVICE NULL
#endif

PDMMicrophones::PDMMicrophones() 
    : dev(nullptr), is_configured(false), 
      is_active(false), sample_rate(0), channels(0), buffer_pos(0)
{
    /* Initialize audio buffer */
    memset(audio_buffer, 0, sizeof(audio_buffer));
    
    /* Try to get MDF device */
    dev = MDF_DEVICE;
    
    /* Defer device tree checking until first use to avoid static init issues */
    LOG_INF("PDM Microphones constructor completed");
}

bool PDMMicrophones::isReady()
{
    /* Check if MDF device is available */
    if (!dev) {
        LOG_ERR("MDF device not available in device tree");
        return false;
    }
    
    if (!device_is_ready(dev)) {
        LOG_ERR("MDF device is not ready");
        return false;
    }
    
    LOG_INF("PDM Microphones ready - Hardware: MP23DB01HPTR");
    LOG_INF("  Left Mic:  CLK=PE9 (MDF1_CCK0), DATA=PE10 (MDF1_SDI0)");
    LOG_INF("  Right Mic: CLK=PF10 (MDF1_CCK1), DATA=PF9 (MDF1_SDI1)");
    
    return true;
}

bool PDMMicrophones::configure(uint32_t pcm_rate, uint8_t channels_num)
{
    if (!isReady()) {
        LOG_ERR("PDM microphones not ready");
        return false;
    }

    if (channels_num > 2) {
        LOG_ERR("Maximum 2 channels supported");
        return false;
    }

    if (pcm_rate < 8000 || pcm_rate > 48000) {
        LOG_ERR("Sample rate must be between 8000-48000 Hz");
        return false;
    }
    
    /* Configure DMIC using Zephyr API */
    struct pcm_stream_cfg stream_cfg = {
        .pcm_rate = pcm_rate,       /* Sample rate */
        .pcm_width = 16,            /* 16-bit PCM */
        .block_size = 64,           /* Block size in samples */
        .mem_slab = NULL            /* Use default memory */
    };
    
    struct dmic_cfg cfg = {
        .io = {
            .min_pdm_clk_freq = 1000000,    /* 1 MHz min PDM clock */
            .max_pdm_clk_freq = 3200000,    /* 3.2 MHz max PDM clock */
            .min_pdm_clk_dc = 40,           /* 40% min duty cycle */
            .max_pdm_clk_dc = 60            /* 60% max duty cycle */
        },
        .streams = &stream_cfg,             /* Point to stream config */
        .channel = {
            .req_chan_map_lo = channels_num == 1 ? 0x1 : 0x3, /* Channel map */
            .req_chan_map_hi = 0,
            .req_num_chan = channels_num,   /* Number of channels */
            .req_num_streams = 1            /* Number of streams */
        }
    };
    
    int ret = dmic_configure(dev, &cfg);
    if (ret < 0) {
        LOG_ERR("Failed to configure DMIC: %d", ret);
        return false;
    }

    /* Store configuration */
    sample_rate = pcm_rate;
    channels = channels_num;
    is_configured = true;

    LOG_INF("PDM microphones configured: %d Hz, %d channels", pcm_rate, channels_num);
    
    return true;
}

bool PDMMicrophones::start()
{
    if (!is_configured) {
        LOG_ERR("PDM microphones not configured");
        return false;
    }

    /* Start DMIC capture using Zephyr API */
    int ret = dmic_trigger(dev, DMIC_TRIGGER_START);
    if (ret < 0) {
        LOG_ERR("Failed to start DMIC capture: %d", ret);
        return false;
    }

    is_active = true;
    buffer_pos = 0;
    
    LOG_INF("PDM microphones started - audio capture active");
    
    return true;
}

bool PDMMicrophones::stop()
{
    if (!is_active) {
        LOG_WRN("PDM microphones not active");
        return false;
    }

    /* Stop DMIC capture using Zephyr API */
    int ret = dmic_trigger(dev, DMIC_TRIGGER_STOP);
    if (ret < 0) {
        LOG_ERR("Failed to stop DMIC capture: %d", ret);
        return false;
    }

    is_active = false;
    buffer_pos = 0;
    
    LOG_INF("PDM microphones stopped");
    return true;
}

size_t PDMMicrophones::read(int16_t* buffer, size_t samples)
{
    if (!is_active || !buffer || samples == 0) {
        return 0;
    }

    /* Try to read from DMIC using Zephyr API */
    void *read_buf;
    size_t size;
    int ret = dmic_read(dev, 0, &read_buf, &size, 0);  /* 0 = no wait */
    
    if (ret < 0) {
        if (ret != -EAGAIN) {
            LOG_ERR("Failed to read from DMIC: %d", ret);
        }
        return 0;
    }
    
    if (read_buf == NULL || size == 0) {
        LOG_DBG("No audio data available");
        return 0;
    }
    
    /* Copy available samples */
    size_t samples_available = size / sizeof(int16_t);
    size_t samples_to_copy = (samples < samples_available) ? samples : samples_available;
    
    if (samples_to_copy > 0) {
        memcpy(buffer, read_buf, samples_to_copy * sizeof(int16_t));
        LOG_DBG("Read %d audio samples from DMIC", samples_to_copy);
    }
    
    return samples_to_copy;
}

bool PDMMicrophones::configure_sound_detection(bool enable, uint32_t threshold)
{
    if (!is_configured) {
        LOG_ERR("PDM microphones not configured");
        return false;
    }

    /* Sound detection would require extended MDF driver features */
    LOG_INF("Sound detection %s (threshold: %d) - extended feature", 
            enable ? "enabled" : "disabled", threshold);
    
    return true;
}
