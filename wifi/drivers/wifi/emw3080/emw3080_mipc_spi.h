/**
 * @file emw3080_mipc_spi.h
 * @brief MIPC-SPI integration for EMW3080
 */

#ifndef EMW3080_MIPC_SPI_H
#define EMW3080_MIPC_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIPC-SPI integration
 * @return 0 on success, negative error code on failure
 */
int emw3080_mipc_spi_init(void);

/**
 * @brief Deinitialize MIPC-SPI integration
 * @return 0 on success, negative error code on failure
 */
int emw3080_mipc_spi_deinit(void);

/**
 * @brief Poll for MIPC responses (call regularly)
 */
void emw3080_mipc_spi_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* EMW3080_MIPC_SPI_H */
