/**
  ******************************************************************************
  * @file    mx_wifi_conf.h
  * @author  MCD Application Team
  * @brief   This file contains configuration macros for the MX WiFi implementation
  *          adapted for Zephyr RTOS on B-U585I-IOT02A board.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef MX_WIFI_CONF_H
#define MX_WIFI_CONF_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

/* MX WiFi Configuration -----------------------------------------------------*/

/* Use SPI interface */
#define MX_WIFI_USE_SPI                                                 (1)

/* Enable network bypass mode for Zephyr networking stack integration */
#define MX_WIFI_NETWORK_BYPASS_MODE                                     (1)

/* Configuration values */
#define MX_WIFI_TX_BUFFER_SIZE                                          (1600)
#define MX_WIFI_RX_BUFFER_SIZE                                          (1600)
#define MX_WIFI_PRODUCT_NAME_SIZE                                       (32)
#define MX_WIFI_PRODUCT_ID_SIZE                                         (32)
#define MX_WIFI_FW_REV_SIZE                                             (16)
#define MX_WIFI_MAC_SIZE                                                (6)
#define MX_WIFI_SCAN_BUF_SIZE                                          (1000)
#define MX_WIFI_MAX_SSID_NAME_SIZE                                      (32)
#define MX_WIFI_MAX_PSWD_NAME_SIZE                                      (64)
#define MX_WIFI_MAX_DETECTED_AP                                         (10)

/* Timeouts */
#define MX_WIFI_CMD_TIMEOUT                                            (10000)
#define MX_WIFI_CONNECT_TIMEOUT                                        (15000)
#define MX_WIFI_SCAN_TIMEOUT                                           (5000)

/* Memory allocation (Zephyr specific) */
#define MX_WIFI_MALLOC                                                  k_malloc
#define MX_WIFI_FREE                                                    k_free

/* OS specific definitions for Zephyr */
#define MX_WIFI_BARE_OS_H                                               (1)

/* Thread priority and stack size for WiFi processing */
#define MX_WIFI_THREAD_PRIORITY                                         (5)
#define MX_WIFI_THREAD_STACK_SIZE                                       (2048)

/* Debug configuration */
#define MX_WIFI_IO_DEBUG                                                (1)

/* Pin definitions for B-U585I-IOT02A board */
/* These will be mapped to Zephyr devicetree nodes */
#define MXCHIP_RESET_Pin                                                1  /* Will map to DT */
#define MXCHIP_NSS_Pin                                                  1  /* Will map to DT */
#define MXCHIP_NOTIFY_Pin                                               1  /* Will map to DT */
#define MXCHIP_FLOW_Pin                                                 1  /* Will map to DT */

/* HAL function adaptation for Zephyr */
#define HAL_GetTick()                                                   k_uptime_get_32()
#define HAL_Delay(ms)                                                   k_msleep(ms)

/* GPIO operations - these will be implemented in our driver */
#define HAL_GPIO_WritePin(port, pin, state)                             emw3080_gpio_write(port, pin, state)
#define HAL_GPIO_ReadPin(port, pin)                                     emw3080_gpio_read(port, pin)

/* Interrupt handling */
#define HAL_NVIC_EnableIRQ(irq)                                         /* Will be handled by Zephyr GPIO callbacks */
#define HAL_NVIC_DisableIRQ(irq)                                        /* Will be handled by Zephyr GPIO callbacks */

/* SPI definitions - will be implemented in our Zephyr driver */
typedef struct spi_handle_s {
    const struct device *spi_dev;
    struct spi_config spi_cfg;
} SPI_HandleTypeDef;

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET = 1
} GPIO_PinState;

/* Function prototypes for Zephyr adaptation layer */
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *pTxData, uint8_t *pRxData, uint16_t Size, uint32_t Timeout);

void emw3080_gpio_write(void *port, uint32_t pin, GPIO_PinState state);
GPIO_PinState emw3080_gpio_read(void *port, uint32_t pin);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_WIFI_CONF_H */
