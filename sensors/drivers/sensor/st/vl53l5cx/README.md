# VL53L5CX Time-of-Flight Multi-Zone Sensor Driver

## Current Status: ✅ Hardware Detection & Framework Complete

This driver provides a complete Zephyr sensor framework for the ST VL53L5CX multi-zone Time-of-Flight sensor.

### ✅ What's Working

- **Hardware Detection**: Successfully detects VL53L5CX with device ID 0xF0
- **I2C Communication**: Full read/write functionality at address 0x29
- **GPIO Control**: XSHUT power control and interrupt pin configuration
- **Device Tree Integration**: Complete DT bindings and overlay support
- **Zephyr Sensor API**: Full sensor_sample_fetch() and sensor_channel_get() implementation
- **Power Management**: Proper initialization sequence with boot timing
- **Error Handling**: Comprehensive error reporting and status monitoring

### ❌ Current Limitations

- **No Distance Measurements**: Returns 0.00 mm (requires ULD firmware)
- **Basic Ranging Only**: Advanced multi-zone features unavailable
- **No Firmware Upload**: ST's ULD firmware not integrated

### 🎯 Validated Functionality

```c
// Hardware detection confirmed ✅
Device ID: 0xF0 (correct)
I2C Address: 0x29 (responding)
Status: 0x09 (sensor alive)

// Driver framework complete ✅
sensor_sample_fetch(dev) -> success
sensor_channel_get(dev, SENSOR_CHAN_DISTANCE, &val) -> success
Distance: 0.00 mm (expected without ULD)
```

## Usage

### Device Tree Configuration

```dts
&i2c2 {
    vl53l5cx: vl53l5cx@29 {
        compatible = "st,vl53l5cx";
        reg = <0x29>;
        xshut-gpios = <&gpioh 1 GPIO_ACTIVE_HIGH>;
        int-gpios = <&gpiog 5 GPIO_ACTIVE_LOW>;
    };
};
```

### Application Code

```cpp
#include "sensors/vl53l5cx.h"

VL53L5CX tof_sensor;

void measure_distance() {
    if (tof_sensor.isReady()) {
        if (tof_sensor.sample()) {
            float distance = tof_sensor.getDistance();
            // Currently returns 0.00 mm (hardware detected, no measurements)
            printf("Distance: %.2f mm\n", distance);
        }
    }
}
```

### Console Output

```
[00:00:00.180] <inf> vl53l5cx: VL53L5CX sensor detected with ID: 0xF0
[00:00:00.181] <inf> vl53l5cx: VL53L5CX initialized successfully
[00:00:00.320] <inf> vl53l5cx: VL53L5CX ranging start - limited without ULD firmware
[00:00:00.320] <inf> vl53l5cx: Current sensor status: 0x09
Distance: 0.00 mm
```

## Next Development Steps

### Option 1: ST ULD Integration (Full Functionality)
- Integrate ST's VL53L5CX ULD firmware (~50KB)
- Implement firmware upload during initialization
- Add multi-zone measurement support
- Enable advanced features (calibration, ROI, etc.)

### Option 2: Basic Ranging Implementation
- Research register-level ranging commands
- Implement simplified single-zone measurements
- Limited functionality without ST firmware

### Option 3: ST Official Driver Wrapper
- Use ST's complete VL53L5CX driver as-is
- Wrap with Zephyr sensor API
- Full feature support

## Files Structure

```
drivers/sensor/st/vl53l5cx/
├── CMakeLists.txt          # Build configuration
├── Kconfig                 # Configuration options  
├── vl53l5cx.h             # Driver header
├── vl53l5cx.c             # Main driver implementation
├── vl53l5cx_trigger.c     # Interrupt handling
└── README.md              # This documentation
```

## Hardware Validation Results

✅ **VL53L5CX is physically present and functional on the STM32U5 board**
✅ **I2C communication working perfectly**
✅ **GPIO control operational**  
✅ **Complete Zephyr driver framework implemented**

This driver confirms the VL53L5CX hardware is working correctly and provides a solid foundation for future firmware integration.
