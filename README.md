# STM32U5 Development Project

A comprehensive Zephyr RTOS development project for the **STM32U585 IoT Discovery Kit (B-U585I-IOT02A)**, featuring sensor integration, wireless connectivity, and embedded systems development examples.

## 🎯 Project Overview

This project demonstrates modern embedded development on the STM32U585 microcontroller using Zephyr RTOS, with a focus on:

- **Comprehensive sensor integration** - Environmental, motion, audio, and distance sensing
- **Wireless connectivity** - Wi-Fi networking with EMW3080 module  
- **Modern C++ sensor abstractions** - Object-oriented sensor driver design
- **Custom driver development** - STM32U5-specific audio (MDF/ADF) and sensor drivers
- **Real-time data processing** - Continuous sensor monitoring and data fusion

## 🏗️ Project Structure

```
├── hello_world/          # Basic Zephyr "Hello World" example
├── sensors/              # Advanced sensor integration project (C++)
├── wifi/                 # Wi-Fi connectivity with EMW3080 module
├── zephyr_env.sh         # Zephyr development environment setup
└── README.md            # This file
```

## 🛠️ Hardware Platform

**STM32U585 IoT Discovery Kit (B-U585I-IOT02A)**
- **MCU**: STM32U585AII6Q (Arm Cortex-M33 with TrustZone)
- **Memory**: 2 MB Flash, 786 KB SRAM
- **Power**: Integrated SMPS for ultra-low power operation
- **Debug**: STLINK-V3E with real-time logging

### Integrated Sensors & Connectivity

| Component | Status | Implementation |
|-----------|--------|----------------|
| **HTS221** (Humidity/Temperature) | ✅ **Working** | Full I²C driver with continuous sampling |
| **IIS2MDC** (Magnetometer) | ✅ **Working** | 3-axis magnetic field measurements |
| **ISM330DHCX** (IMU) | ✅ **Working** | 6-axis accelerometer + gyroscope |
| **LPS22HH** (Pressure) | ✅ **Working** | Barometric pressure + temperature |
| **VEML6030/7700** (Ambient Light) | ✅ **Working** | Ambient light sensing with lux calculations |
| **VL53L5CX** (Time-of-Flight) | ⚠️ **Partial** | Hardware detection (ULD firmware pending) |
| **MP23DB01HPTR** (PDM Mics) | ⚠️ **In Progress** | Custom STM32U5 MDF driver development |
| **EMW3080** (Wi-Fi) | 🔧 **Under Development** | 802.11 b/g/n connectivity via SPI |
| **STM32WB5MMG** (Bluetooth LE) | ❌ **Future** | BLE 5.0 support planned |

## 🚀 Getting Started

### Prerequisites

1. **Zephyr SDK** (v0.17.4+) with ARM toolchain
2. **West** build tool for Zephyr
3. **STM32CubeProgrammer** for flashing
4. **macOS development environment** (project configured for macOS)

### Environment Setup

```bash
# Source the Zephyr environment
source zephyr_env.sh

# Verify toolchain installation
arm-zephyr-eabi-gcc --version
west --version
```

### Build & Flash Examples

#### Hello World (Basic)
```bash
cd hello_world
west build -b b_u585i_iot02a -p always
west flash
```

#### Sensor Integration (Advanced)
```bash
cd sensors
west build -b b_u585i_iot02a -p always
west flash
```

#### Wi-Fi Connectivity
```bash
cd wifi
west build -b b_u585i_iot02a -p always
west flash
```

## 📊 Project Details

### 1. Hello World (`hello_world/`)
**Language**: C  
**Purpose**: Basic Zephyr introduction and board verification

Simple "Hello World" application that prints system information every second. Perfect for:
- Verifying toolchain setup
- Testing board connectivity
- Learning basic Zephyr development

### 2. Sensor Integration (`sensors/`)
**Language**: C++  
**Purpose**: Comprehensive sensor data acquisition and processing

Advanced sensor monitoring application featuring:
- **Real-time sensor fusion** - Continuous monitoring of all operational sensors
- **Object-oriented design** - Modern C++ sensor class abstractions
- **Custom drivers** - STM32U5-specific MDF/ADF audio interface
- **Data processing** - RMS audio level calculation, motion detection
- **Sensor status reporting** - Detailed logging and error handling

**Key Features**:
- Environmental monitoring (temperature, humidity, pressure, light)
- Motion sensing (3-axis magnetometer, 6-axis IMU)
- Audio capture (PDM microphones with RMS calculation)
- Distance measurement (Time-of-Flight sensor)

### 3. Wi-Fi Connectivity (`wifi/`)
**Language**: C  
**Purpose**: Network connectivity with EMW3080 Wi-Fi module

Wi-Fi networking application providing:
- **Station mode support** - Connect to existing Wi-Fi networks
- **Network scanning** - Discover available access points
- **Shell interface** - Interactive Wi-Fi and network commands
- **DHCP/DNS support** - Automatic network configuration
- **Protocol testing** - SPI communication and MIPC protocol validation

## 🔧 Development Features

### Custom Driver Development
- **STM32U5 MDF/ADF driver** for PDM microphones
- **VL53L5CX Time-of-Flight** sensor integration
- **EMW3080 Wi-Fi** module with SPI interface

### Modern Development Practices
- **C++17** sensor abstractions with consistent interfaces
- **Zephyr device tree** configuration
- **CMake** build system with modular driver organization
- **Comprehensive logging** with configurable log levels

### Debugging & Monitoring
- **Real-time sensor data** via UART console
- **STLINK-V3E** debugging with GDB support
- **West flash** integration for rapid development cycles

## 📋 Development Roadmap

### Current Focus (Q3 2024)
- ✅ Core sensor integration complete
- 🔧 PDM microphone driver optimization
- 🔧 Wi-Fi connectivity stabilization

### Next Phase
- 📅 VL53L5CX ULD firmware integration
- 📅 Bluetooth LE (STM32WB5MMG) implementation
- 📅 STSAFE-A110 secure element integration

### Future Enhancements
- 📅 Sensor data logging to external memory
- 📅 Over-the-air (OTA) firmware updates
- 📅 Cloud connectivity and IoT integration

## 🏷️ Technical Specifications

**Development Environment**:
- Zephyr RTOS v4.2.0+
- ARM Cortex-M33 toolchain
- West build system
- macOS-optimized development setup

**Communication Interfaces**:
- I²C2 (sensors), SPI2 (Wi-Fi), UART4 (Bluetooth)
- MDF/ADF (audio), OCTOSPI (external memory)
- USB Type-C (debug/programming)

**Power Management**:
- Ultra-low power design with SMPS
- Configurable sensor sampling rates
- Sleep mode support (future)

## 📄 License

SPDX-License-Identifier: Apache-2.0

---

**Target Audience**: Embedded systems developers, IoT engineers, STM32 enthusiasts, and Zephyr RTOS learners looking for comprehensive sensor integration and wireless connectivity examples.
