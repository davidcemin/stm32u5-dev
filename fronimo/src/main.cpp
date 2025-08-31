#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sensors/hts221.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("B-U585I-IOT02A C++ Sensor Example");

    HTS221 hts;

    if (!hts.isReady()) {
        LOG_ERR("HTS221 not available");
        return 0;
    }

    while (true) {
        if (hts.sample()) {
            float temp = hts.getTemperature();
            float hum = hts.getHumidity();
            LOG_INF("Temperature: %d.%02d °C, Humidity: %d.%02d %%", 
                    (int)temp, (int)((temp - (int)temp) * 100),
                    (int)hum, (int)((hum - (int)hum) * 100));
        }
        k_sleep(K_SECONDS(2));
    }
}
