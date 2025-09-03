#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>

/**
 * @brief PDM Microphone Manager for MP23DB01HPTR
 * 
 * This class manages dual MP23DB01HPTR PDM microphones connected to the
 * STM32U5 MDF (Multi-rate Digital Filter) peripheral.
 * 
 * Hardware connections on B-U585I-IOT02A:
 * - Left Mic:  CLK=PE9 (MDF1_CCK0), DATA=PE10 (MDF1_SDI0)  
 * - Right Mic: CLK=PF10 (MDF1_CCK1), DATA=PF9 (MDF1_SDI1)
 * 
 * Note: This implementation creates a simple interface for the PDM microphones.
 * A complete MDF driver would require extensive STM32U5 peripheral integration.
 */
class PDMMicrophones {
private:
    const struct device* dev;
    bool is_configured;
    bool is_active;
    uint32_t sample_rate;
    uint8_t channels;
    
    // Audio buffer management
    static constexpr size_t BUFFER_SIZE = 64; // Reduced from 512
    alignas(4) int16_t audio_buffer[BUFFER_SIZE];
    size_t buffer_pos;

public:
    PDMMicrophones();
    
    /**
     * @brief Check if microphones are ready
     * @return true if initialized and ready
     */
    bool isReady();  // Changed to non-const to allow lazy initialization
    
    /**
     * @brief Configure microphones
     * @param pcm_rate Target sample rate (8000-48000 Hz)
     * @param channels Number of channels (1 or 2)
     * @return true on success
     */
    bool configure(uint32_t pcm_rate = 16000, uint8_t channels = 1);
    
    /**
     * @brief Start audio capture
     * @return true on success
     */
    bool start();
    
    /**
     * @brief Stop audio capture  
     * @return true on success
     */
    bool stop();
    
    /**
     * @brief Read audio samples (non-blocking)
     * @param buffer Buffer to store samples
     * @param samples Number of samples to read
     * @return Number of samples actually read
     */
    size_t read(int16_t* buffer, size_t samples);
    
    /**
     * @brief Get current configuration
     * @return Current sample rate in Hz
     */
    uint32_t get_sample_rate() const { return sample_rate; }
    
    /**
     * @brief Get number of active channels
     * @return Number of channels (1 or 2)
     */
    uint8_t get_channels() const { return channels; }
    
    /**
     * @brief Check if microphones are active
     * @return true if capturing audio
     */
    bool is_capturing() const { return is_active; }
    
    /**
     * @brief Enable/disable sound detection
     * @param enable Enable sound detection
     * @param threshold Detection threshold
     * @return true on success
     */
    bool configure_sound_detection(bool enable, uint32_t threshold = 1000);
};
