/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#ifndef CONFIG_EMW3080_SPI_LOG_LEVEL
#define CONFIG_EMW3080_SPI_LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#endif
LOG_MODULE_REGISTER(emw3080_spi, CONFIG_EMW3080_SPI_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include "emw3080.h"
#include "emw3080_spi.h"
#include "emw3080_slip.h"

/* SPI configuration for EMW3080B */
static struct spi_config emw3080_spi_cfg = {
    /* Default to Mode 0 (CPOL=0, CPHA=0); can be overridden by DT or Kconfig */
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB
#ifdef CONFIG_EMW3080_SPI_MODE3
                 | SPI_MODE_CPOL | SPI_MODE_CPHA
#endif
                 ,
#ifdef CONFIG_EMW3080_SPI_FREQ_HZ
    .frequency = CONFIG_EMW3080_SPI_FREQ_HZ,
#else
    .frequency = 8000000,
#endif
    .slave = 0,
};

/* Optional CS control populated from devicetree */
static struct spi_cs_control emw3080_cs_ctrl;
static struct gpio_dt_spec emw3080_flow_gpio; /* optional wake/data-ready */

/* Mutex for SPI access synchronization */
static K_MUTEX_DEFINE(spi_mutex);

static int wait_flow_high(k_timeout_t to)
{
    if (!emw3080_flow_gpio.port) {
        return 0; /* no flow pin configured */
    }
    if (!device_is_ready(emw3080_flow_gpio.port)) {
        return -ENODEV;
    }
    int64_t end = k_uptime_get() + k_ticks_to_ms_floor64(to.ticks);
    while (k_uptime_get() < end) {
        int val = gpio_pin_get_dt(&emw3080_flow_gpio);
        if (val > 0) return 0; /* high */
        k_busy_wait(50);
    }
    return -ETIMEDOUT;
}

/* Wait for EMW3080B to be ready for communication (FLOW-based, no SPI bytes) */
int emw3080_spi_wait_ready(const struct device *spi_dev, uint32_t timeout_ms)
{
    ARG_UNUSED(spi_dev);
    int64_t end = k_uptime_get() + timeout_ms;
    while (k_uptime_get() < end) {
        if (!emw3080_flow_gpio.port) {
            return 0; /* no flow pin, assume ready */
        }
        int val = gpio_pin_get_dt(&emw3080_flow_gpio);
        if (val > 0) {
            return 0;
        }
        k_msleep(1);
    }
    return -ETIMEDOUT;
}

/* Check if data is available for reading */
static bool emw3080_spi_data_available(const struct device *spi_dev)
{
    ARG_UNUSED(spi_dev);
    if (!emw3080_flow_gpio.port) {
        return true; /* no flow pin, poll anyway */
    }
    int val = gpio_pin_get_dt(&emw3080_flow_gpio);
    return val > 0;
}

/* Low-level SPI transceive with proper CS handling */
int emw3080_spi_transceive(const struct device *spi_dev, 
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len)
{
    /* CRITICAL: Add extensive safety checks to prevent crashes */
    LOG_DBG("EMW3080 SPI: transceive tx=%zu rx=%zu", tx_len, rx_len);
    
    if (!spi_dev) {
        LOG_ERR("EMW3080 SPI: NULL SPI device pointer");
        return -ENODEV;
    }
    
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("EMW3080 SPI: Device %s not ready", spi_dev->name);
        return -ENODEV;
    }
    
    /* Validate the SPI device API before using it */
    if (!spi_dev->api) {
        LOG_ERR("EMW3080 SPI: Device has no API");
        return -ENODEV;
    }
    
    const struct spi_driver_api *api = (const struct spi_driver_api *)spi_dev->api;
    if (!api->transceive) {
        LOG_ERR("EMW3080 SPI: Device API has no transceive function");
        return -ENODEV;
    }
    
    /* Validate buffer parameters */
    if (tx_len > 0 && !tx_buf) {
        LOG_ERR("EMW3080 SPI: TX buffer NULL but length > 0");
        return -EINVAL;
    }
    
    if (rx_len > 0 && !rx_buf) {
        LOG_ERR("EMW3080 SPI: RX buffer NULL but length > 0");
        return -EINVAL;
    }
    
    /* No arbitrary small cap; upper bounds enforced by protocol-specific send/recv */
    
    LOG_DBG("EMW3080 SPI: Safety checks passed, attempting SPI transaction");
    
    /* SAFE APPROACH: Try the transaction with error handling */
    struct spi_buf tx_bufs[] = {
        {.buf = (void *)tx_buf, .len = tx_len}
    };
    struct spi_buf rx_bufs[] = {
        {.buf = rx_buf, .len = rx_len}
    };
    
    struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = (tx_len > 0) ? 1 : 0};
    struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = (rx_len > 0) ? 1 : 0};
    
    /* Log what we're about to do */
    LOG_DBG("EMW3080 SPI: spi_transceive freq=%d", emw3080_spi_cfg.frequency);
    
    /* CRITICAL: Try to detect SPI configuration issues before they crash */
    if (emw3080_spi_cfg.frequency == 0) {
        LOG_ERR("EMW3080 SPI: Invalid SPI frequency (0)");
        return -EINVAL;
    }
    
    /* Attempt the actual SPI transaction */
    int ret = spi_transceive(spi_dev, &emw3080_spi_cfg, &tx_set, &rx_set);
    
    if (ret < 0) {
        LOG_ERR("EMW3080 SPI: Transaction failed with error %d", ret);
        
        /* Log more details about the failure */
        LOG_ERR("EMW3080 SPI: Device: %s", spi_dev->name);
        LOG_ERR("EMW3080 SPI: Config - freq:%d, op:0x%08x", 
                emw3080_spi_cfg.frequency, emw3080_spi_cfg.operation);
        
        return ret;
    }
    
    LOG_DBG("EMW3080 SPI: Transaction completed successfully");
    return ret;
}

/* Send data frame to EMW3080B */
int emw3080_spi_send_frame(const struct device *spi_dev,
                          const uint8_t *data, size_t data_len)
{
    if (!spi_dev || !data) {
        return -EINVAL;
    }
    
    if (data_len > EMW3080_SPI_MAX_PAYLOAD_SIZE) {
        LOG_ERR("Data too large: %zu > %d", data_len, EMW3080_SPI_MAX_PAYLOAD_SIZE);
        return -EINVAL;
    }
    
    /* Acquire mutex for thread-safe access */
    int ret = k_mutex_lock(&spi_mutex, K_MSEC(1000));
    if (ret != 0) {
        LOG_ERR("Failed to acquire SPI mutex");
        return ret;
    }
    
    /* Wait for FLOW high to avoid clocking when device isn't ready */
    (void)wait_flow_high(K_MSEC(5));

    /* Prepare MX WiFi compatible 8-byte header (type,len,lenx,dummy[3]) */
    struct emw3080_spi_header h_tx = {0};
    struct emw3080_spi_header h_rx = {0};
    h_tx.type = EMW3080_SPI_WRITE;
    h_tx.len = sys_cpu_to_le16((uint16_t)data_len);
    h_tx.lenx = sys_cpu_to_le16((uint16_t)(data_len ^ 0xFFFF));

    bool hold_on_cs = IS_ENABLED(CONFIG_EMW3080_SPI_HOLD_ON_CS);
    struct spi_config cfg = emw3080_spi_cfg;
    if (hold_on_cs) {
        cfg.operation |= SPI_HOLD_ON_CS;
        LOG_DBG("HOLD_ON_CS enabled for TX");
    }

    /* Exchange headers in full-duplex */
    struct spi_buf tx_hdr_buf = { .buf = &h_tx, .len = sizeof(h_tx) };
    struct spi_buf rx_hdr_buf = { .buf = &h_rx, .len = sizeof(h_rx) };
    struct spi_buf_set tx_hdr = { .buffers = &tx_hdr_buf, .count = 1 };
    struct spi_buf_set rx_hdr = { .buffers = &rx_hdr_buf, .count = 1 };
    LOG_DBG("Header TX: type=0x%02x len=%u lenx=0x%04x", h_tx.type, data_len, data_len ^ 0xFFFF);
    ret = spi_transceive(spi_dev, &cfg, &tx_hdr, &rx_hdr);
    if (ret) {
        LOG_ERR("Header xfer failed: %d", ret);
        if (hold_on_cs) { spi_release(spi_dev, &cfg); }
        k_mutex_unlock(&spi_mutex);
        return ret;
    }

    /* If device has data to send concurrently, do not send now; let upper layer receive first */
    uint16_t dev_len = sys_le16_to_cpu(h_rx.len);
    uint16_t dev_lenx = sys_le16_to_cpu(h_rx.lenx);
    if ((dev_len == 0 && dev_lenx == 0 && h_rx.type == 0x00) ||
        (dev_len == 0xFFFF && dev_lenx == 0xFFFF && h_rx.type == 0xFF)) {
        /* Idle peer header, proceed */
    } else if (((dev_len ^ dev_lenx) != 0xFFFF) || (h_rx.type != EMW3080_SPI_READ)) {
        LOG_WRN("Peer header unexpected: type=0x%02x len=%04x lenx=%04x", h_rx.type, dev_len, dev_lenx);
    } else if (dev_len > 0) {
        /* Abort TX now; upper layer should receive and retry */
        if (hold_on_cs) { spi_release(spi_dev, &cfg); }
        k_mutex_unlock(&spi_mutex);
        return -EAGAIN;
    }

    /* Transmit our payload */
    size_t datalen = data_len;
    struct spi_buf tx_payload = { .buf = (void *)data, .len = datalen };
    struct spi_buf rx_payload = { .buf = NULL, .len = datalen };
    struct spi_buf_set txp = { .buffers = &tx_payload, .count = (datalen ? 1 : 0) };
    struct spi_buf_set rxp = { .buffers = &rx_payload, .count = (datalen ? 1 : 0) };
    /* Tiny settle after header before pushing payload (tuned) */
    k_busy_wait(25);
    ret = spi_transceive(spi_dev, &cfg, &txp, &rxp);
    if (hold_on_cs) { spi_release(spi_dev, &cfg); }
    
    k_mutex_unlock(&spi_mutex);
    return ret;
}

/* Receive data frame from EMW3080B */
int emw3080_spi_recv_frame(const struct device *spi_dev,
                          uint8_t *data, size_t max_len, size_t *received_len)
{
    if (!spi_dev || !data || !received_len) {
        return -EINVAL;
    }
    
    *received_len = 0;
    
    /* Acquire mutex for thread-safe access */
    int ret = k_mutex_lock(&spi_mutex, K_MSEC(1000));
    if (ret != 0) {
        LOG_ERR("Failed to acquire SPI mutex");
        return ret;
    }
    
    /* FLOW is only a hint; proceed even if low to avoid missing windows */
    /* Exchange 8-byte headers in full-duplex; host sends WRITE header with len=0 to poll */
    bool hold_on_cs = IS_ENABLED(CONFIG_EMW3080_SPI_HOLD_ON_CS);
    struct spi_config cfg = emw3080_spi_cfg;
    if (hold_on_cs) {
        cfg.operation |= SPI_HOLD_ON_CS;
        LOG_DBG("HOLD_ON_CS enabled for RX");
    }
    struct emw3080_spi_header h_tx = {0};
    struct emw3080_spi_header h_rx = {0};
    h_tx.type = EMW3080_SPI_WRITE; /* poll header; len=0 */
    h_tx.len = sys_cpu_to_le16(0);
    h_tx.lenx = sys_cpu_to_le16(0xFFFF);
    struct spi_buf tx_hdr_buf = { .buf = &h_tx, .len = sizeof(h_tx) };
    struct spi_buf rx_hdr_buf = { .buf = &h_rx, .len = sizeof(h_rx) };
    struct spi_buf_set tx_hdr = { .buffers = &tx_hdr_buf, .count = 1 };
    struct spi_buf_set rx_hdr = { .buffers = &rx_hdr_buf, .count = 1 };

    LOG_DBG("Polling header... (hold_cs=%s)", hold_on_cs ? "yes" : "no");
    ret = spi_transceive(spi_dev, &cfg, &tx_hdr, &rx_hdr);
    if (ret != 0) {
        LOG_ERR("Failed to read frame header: %d", ret);
        if (hold_on_cs) {
            spi_release(spi_dev, &cfg);
        }
        k_mutex_unlock(&spi_mutex);
        return ret;
    }
    /* Validate header */
    uint16_t payload_len = sys_le16_to_cpu(h_rx.len);
    uint16_t payload_lenx = sys_le16_to_cpu(h_rx.lenx);
    LOG_DBG("Peer header: type=0x%02x len=%04x lenx=%04x", h_rx.type, payload_len, payload_lenx);
    if (((payload_len ^ payload_lenx) != 0xFFFF) || (h_rx.type != EMW3080_SPI_READ)) {
        /* Treat all-zero idle header as no data */
        if ((h_rx.type == 0x00 && payload_len == 0 && payload_lenx == 0) ||
            (h_rx.type == 0xFF && payload_len == 0xFFFF && payload_lenx == 0xFFFF)) {
            LOG_DBG("Idle header (no data)");
            if (hold_on_cs) { spi_release(spi_dev, &cfg); }
            k_mutex_unlock(&spi_mutex);
            return -ENODATA;
        }
        static int bad_hdr_cnt;
        if ((bad_hdr_cnt++ % 4) == 0) {
            LOG_WRN("Invalid/Unexpected header: type=0x%02x len=%04x lenx=%04x", h_rx.type, payload_len, payload_lenx);
        }
        LOG_DBG("Header dump (8B): %02x %02x %02x %02x %02x %02x %02x %02x",
                ((uint8_t *)&h_rx)[0], ((uint8_t *)&h_rx)[1], ((uint8_t *)&h_rx)[2], ((uint8_t *)&h_rx)[3],
                ((uint8_t *)&h_rx)[4], ((uint8_t *)&h_rx)[5], ((uint8_t *)&h_rx)[6], ((uint8_t *)&h_rx)[7]);
        if (hold_on_cs) { spi_release(spi_dev, &cfg); }
        k_mutex_unlock(&spi_mutex);
        return -EBADMSG;
    }
    
    if (payload_len > max_len) {
        LOG_ERR("Received frame too large: %u > %zu", payload_len, max_len);
        k_mutex_unlock(&spi_mutex);
        return -ENOMEM;
    }
    
    if (payload_len > 0) {
        /* Read payload data in same CS session */
        LOG_DBG("Reading %u bytes of payload...", payload_len);
        struct spi_buf tx_payload = { .buf = NULL, .len = payload_len };
        struct spi_buf rx_payload = { .buf = data, .len = payload_len };
        struct spi_buf_set txp = { .buffers = &tx_payload, .count = 1 };
        struct spi_buf_set rxp = { .buffers = &rx_payload, .count = 1 };
    /* Tiny settling delay helps some firmwares after header (tuned) */
    k_busy_wait(25);
        ret = spi_transceive(spi_dev, &cfg, &txp, &rxp);
        if (ret == 0) {
            *received_len = payload_len;
            LOG_DBG("Successfully received frame: %u bytes", payload_len);
            
            /* Debug: print first few bytes of payload */
            if (payload_len > 0) {
                LOG_DBG("Payload preview: %02x %02x %02x %02x...", 
                       data[0], 
                       payload_len > 1 ? data[1] : 0,
                       payload_len > 2 ? data[2] : 0,
                       payload_len > 3 ? data[3] : 0);
            }
        } else {
            LOG_ERR("Failed to read payload: %d", ret);
    }
    } else {
    LOG_DBG("Empty frame (no payload)");
        if (hold_on_cs) { spi_release(spi_dev, &cfg); }
        k_mutex_unlock(&spi_mutex);
        return -ENODATA;
    }
    if (hold_on_cs) {
        spi_release(spi_dev, &cfg);
    }
    
    k_mutex_unlock(&spi_mutex);
    return ret;
}

/* Initialize SPI communication with EMW3080B */
int emw3080_spi_init(const struct device *spi_dev)
{
    if (!spi_dev) {
        LOG_ERR("No SPI device provided");
        return -EINVAL;
    }
    
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }
    
    LOG_INF("Initializing EMW3080B SPI on device: %s (freq=%u)", spi_dev->name, emw3080_spi_cfg.frequency);
    
    /* Initialize mutex */
    k_mutex_init(&spi_mutex);
    
    /* Wait for device to be ready */
    LOG_INF("Waiting for EMW3080B to be ready...");
    int ret = emw3080_spi_wait_ready(spi_dev, 2000);
    if (ret != 0) {
        LOG_WRN("Device not ready, but continuing initialization");
    }
    
    /* Apply conservative runtime overrides if configured */
#ifdef CONFIG_EMW3080_SPI_MODE3
    emw3080_spi_cfg.operation |= (SPI_MODE_CPOL | SPI_MODE_CPHA);
#else
    emw3080_spi_cfg.operation &= ~(SPI_MODE_CPOL | SPI_MODE_CPHA);
#endif
#ifdef CONFIG_EMW3080_SPI_FREQ_HZ
    emw3080_spi_cfg.frequency = CONFIG_EMW3080_SPI_FREQ_HZ;
#endif

    LOG_INF("EMW3080B SPI interface initialized for MIPC protocol (op=0x%08x, freq=%u)",
            emw3080_spi_cfg.operation, emw3080_spi_cfg.frequency);
    
    /* Drain any pending frames left by boot only if FLOW is high */
    if (emw3080_flow_gpio.port && gpio_pin_get_dt(&emw3080_flow_gpio) > 0) {
        uint8_t dump[64];
        size_t rcv_len = 0;
        int r = emw3080_spi_recv_frame(spi_dev, dump, sizeof(dump), &rcv_len);
        if (r == 0 && rcv_len > 0) {
            LOG_DBG("Drained %zu bytes of pending data", rcv_len);
        }
    }
    return 0;
}

/* Apply DT-provided SPI settings (frequency, CS) to our static SPI config */
int emw3080_spi_set_dt_spec(const struct spi_dt_spec *spec)
{
    if (!spec) {
        return -EINVAL;
    }
    /* Validate bus and optional CS GPIO readiness */
    if (!spi_is_ready_dt(spec)) {
        return -ENODEV;
    }

    /* Apply full config from DT spec */
    emw3080_spi_cfg.frequency = spec->config.frequency;
    emw3080_spi_cfg.operation = spec->config.operation;
    emw3080_spi_cfg.slave = spec->config.slave;

    /* Copy CS control (it's a struct in this Zephyr version) */
    emw3080_cs_ctrl = spec->config.cs;
    emw3080_spi_cfg.cs = emw3080_cs_ctrl;

    LOG_INF("Applied SPI DT spec: freq=%u, slave=%u, cs_gpio_port=%p",
            emw3080_spi_cfg.frequency, emw3080_spi_cfg.slave,
            (void *)emw3080_spi_cfg.cs.gpio.port);
    return 0;
}

int emw3080_spi_set_flow_gpio(const struct gpio_dt_spec *flow)
{
    if (!flow) {
        emw3080_flow_gpio.port = NULL;
        return 0;
    }
    emw3080_flow_gpio = *flow;
    if (!device_is_ready(emw3080_flow_gpio.port)) {
        return -ENODEV;
    }
    /* Input with pull-up if supported */
    gpio_pin_configure_dt(&emw3080_flow_gpio, GPIO_INPUT);
    LOG_INF("Configured FLOW GPIO: port=%p pin=%u", (void *)emw3080_flow_gpio.port, emw3080_flow_gpio.pin);
    return 0;
}

int emw3080_spi_set_mode_flags(uint32_t mode_flags)
{
    /* Preserve word size and MSB-order; update CPOL/CPHA and other mode bits */
    emw3080_spi_cfg.operation &= ~(SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_MODE_LOOP | SPI_MODE_MASK);
    emw3080_spi_cfg.operation |= (mode_flags & (SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_MODE_LOOP));
    LOG_INF("EMW3080 SPI: mode flags set: 0x%08x", emw3080_spi_cfg.operation);
    return 0;
}

int emw3080_spi_set_frequency(uint32_t hz)
{
    if (hz < 100000 || hz > 24000000) {
        return -EINVAL;
    }
    emw3080_spi_cfg.frequency = hz;
    LOG_INF("EMW3080 SPI: frequency set: %u Hz", hz);
    return 0;
}

/* ================================== */
/* SLIP-Enhanced SPI Functions */
/* ================================== */

int emw3080_spi_send_slip_frame(const struct device *spi_dev,
                                const uint8_t *data, size_t data_len)
{
    if (!spi_dev || !data || data_len == 0) {
        return -EINVAL;
    }
    
    /* Encode data with SLIP framing */
    uint8_t slip_buffer[emw3080_slip_max_encoded_size(data_len)];
    uint16_t encoded_len;
    
    int ret = emw3080_slip_encode(data, data_len, slip_buffer, sizeof(slip_buffer), &encoded_len);
    if (ret != 0) {
        LOG_ERR("SLIP encoding failed: %d", ret);
        return ret;
    }
    
    LOG_DBG("Sending SLIP frame: %d bytes encoded to %d bytes", data_len, encoded_len);
    
    /* Send SLIP-encoded frame via standard SPI */
    return emw3080_spi_send_frame(spi_dev, slip_buffer, encoded_len);
}

int emw3080_spi_recv_slip_frame(const struct device *spi_dev,
                                uint8_t *data, size_t max_len, size_t *received_len)
{
    if (!spi_dev || !data || !received_len) {
        return -EINVAL;
    }
    
    /* Receive raw SPI frame */
    uint8_t slip_buffer[EMW3080_SPI_MAX_FRAME_SIZE];
    size_t slip_len;
    
    int ret = emw3080_spi_recv_frame(spi_dev, slip_buffer, sizeof(slip_buffer), &slip_len);
    if (ret != 0) {
        return ret;
    }
    
    if (slip_len == 0) {
        *received_len = 0;
        return 0;
    }
    
    /* Decode SLIP frame */
    uint16_t decoded_len;
    ret = emw3080_slip_decode(slip_buffer, slip_len, data, max_len, &decoded_len);
    if (ret != 0) {
        if (ret == -EAGAIN) {
            /* No complete frame yet */
            *received_len = 0;
            return 0;
        }
        LOG_ERR("SLIP decoding failed: %d", ret);
        return ret;
    }
    
    *received_len = decoded_len;
    LOG_DBG("Received SLIP frame: %d bytes decoded from %d bytes", decoded_len, slip_len);
    
    return 0;
}
