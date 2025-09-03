# B-U585I-IOT02A Discovery Kit – Sensor & Connectivity Overview

The **B-U585I-IOT02A** is an IoT discovery kit featuring the **STM32U585AII6Q** (Arm Cortex-M33, TrustZone, 2 MB Flash, 786 KB SRAM).  
It integrates multiple environmental, motion, audio, and security sensors, plus Wi-Fi and Bluetooth connectivity.

---

## 📊 Sensor Implementation Status

| Sensor | Status | Implementation | Notes |
|--------|--------|----------------|-------|
| **HTS221** (Humidity & Temperature) | ✅ **Working** | Full I²C driver with sampling | Temperature/humidity readings operational |
| **IIS2MDC** (Magnetometer) | ✅ **Working** | Full I²C driver with 3-axis readings | Magnetic field measurements operational |
| **ISM330DHCX** (IMU) | ✅ **Working** | Full I²C driver with accel + gyro | 6-axis motion sensing operational |
| **LPS22HH** (Pressure) | ✅ **Working** | Full I²C driver with one-shot mode | Barometric pressure + temperature operational |
| **VEML6030/VEML7700** (Ambient Light) | ✅ **Working** | Full I²C driver with lux calculations | Ambient light sensing operational |
| **VL53L5CX** (Time-of-Flight) | ⚠️ **Partial** | Hardware detection only | Requires ULD firmware for measurements |
| **MP23DB01HPTR** (PDM Microphones) | ⚠️ **In Progress** | Custom STM32U5 MDF driver | Hardware functional, data acquisition under development |
| **STSAFE-A110** (Secure Element) | ❌ **Not Implemented** | - | Security features not yet implemented |

### 🎯 Fully Operational Sensors (6/8)
- **Environmental**: HTS221 (temp/humidity), LPS22HH (pressure), VEML6030 (ambient light)
- **Motion**: IIS2MDC (magnetometer), ISM330DHCX (accelerometer + gyroscope)
- **Distance**: VL53L5CX (hardware detection working)

### 🔧 In Development
- **Audio**: MP23DB01HPTR PDM microphones - MDF interface configured, sampling implementation in progress
- **Distance**: VL53L5CX - needs ULD firmware integration for full ranging functionality

### 📋 Not Yet Started
- **Security**: STSAFE-A110 secure element

---

## 🌐 Connectivity Implementation Status

| Module | Status | Implementation | Notes |
|--------|--------|----------------|-------|
| **MXCHIP EMW3080** (Wi-Fi) | ❌ **Not Implemented** | - | 802.11 b/g/n connectivity not yet implemented |
| **STM32WB5MMG** (Bluetooth LE) | ❌ **Not Implemented** | - | BLE 5.0 connectivity not yet implemented |

### Current Focus
The project currently focuses on **sensor data acquisition and processing**. Wireless connectivity modules will be implemented in future development phases.

---

## 🚀 Current Application Features

### Real-Time Sensor Monitoring
The main application (`src/main.cpp`) provides continuous monitoring of all operational sensors:

- **Environmental Data**: Temperature, humidity, barometric pressure, ambient light
- **Motion Sensing**: 3-axis magnetometer, 6-axis IMU (accelerometer + gyroscope)  
- **Audio Processing**: PDM microphone data acquisition with RMS level calculation
- **Distance Measurement**: VL53L5CX hardware detection (full ranging pending ULD firmware)

### Sensor Fusion Ready
All sensor classes implement consistent interfaces for:
- ✅ Device detection and initialization
- ✅ Data sampling and conversion
- ✅ Error handling and logging
- ✅ Power management (where applicable)

### Development Platform
- **Zephyr RTOS**: v4.2.0+ with STM32U5 HAL integration
- **Language**: C++ with modern sensor class abstractions
- **Build System**: West/CMake with device tree configuration
- **Debug**: STLINK-V3E with real-time logging

---

## 🖥 MCU
- **STM32U585AII6Q**
  - Arm Cortex-M33 with TrustZone
  - 2 MB Flash, 786 KB SRAM
  - Integrated SMPS for ultra-low power

---

## 🎛 On-board Sensors

| Sensor | Part Number | Function | Interface | MCU Pins |
|--------|-------------|----------|-----------|----------|
| Digital microphones (×2) | MP23DB01HPTR | Omnidirectional MEMS, PDM output | **MDF/ADF (PDM interface)** | PE9 (CLK0), PE10 (SDINx), PB1 (SDIN0), PF10 (CLK1) |
| Humidity & temperature | HTS221 | RH + Temp | **I²C2** | PH4 (SCL), PH5 (SDA) |
| Magnetometer | IIS2MDCTR | 3-axis magnetometer | **I²C2 + INT** | PH4/PH5 + PD10 (INT) |
| IMU (accel + gyro) | ISM330DHCX | 3-axis accel + 3-axis gyro | **I²C2 + INT** | PH4/PH5 + PE11 (INT1) |
| Pressure | LPS22HH | Barometer (260–1260 hPa) | **I²C2 + INT** | PH4/PH5 + PG2 (INT) |
| Time-of-Flight / gesture | VL53L5CX | ToF ranging + gesture detection | **I²C2 + GPIOs** | PH4/PH5 + PH1 (XSHUT) + PG5 (GPIO1) |
| Ambient light | VEML6030 or VEML3235 | ALS | **I²C2** | PH4, PH5 |
| Secure element | STSAFE-A110 | Authentication / security | **I²C2 + EN** | PH4, PH5 + PF11 (EN) |

---

## 📡 Connectivity

| Module | Part Number | Function | Interface | MCU Pins |
|--------|-------------|----------|-----------|----------|
| Wi-Fi | MXCHIP EMW3080 | 802.11 b/g/n, integrated TCP/IP | **SPI2** | PD1 (SCK), PD3 (MISO), PD4 (MOSI), PB12 (NSS), PF15 (Chip_EN), PG15 (FLOW), PD14 (NOTIFY) |
| Bluetooth LE | STM32WB5MMG | BLE 5.0 (also supports Zigbee / Thread) | **UART4 + GPIO** | PC10 (TX), PC11 (RX), PA0 (WKUP_B), PA2/PA3 (UART), PG6 (control) |

---

## 🗂 Memory

- **MX25LM51245G** – 512 Mbit Octo-SPI Flash (OCTOSPI1)
- **APS6408L** – 64 Mbit Octo-SPI PSRAM (OCTOSPI2)
- **M24256-DFMC6TG** – 256 Kbit I²C EEPROM (I²C2)

---

## 🔌 Expansion & Debug

- **Arduino Uno V3 connectors**
- **2 × STMod+ connectors** (UART/SPI/I²C selectable)
- **Pmod™ connector** (shared with STMod+ CN3)
- **Camera connector (CN7)**
- **USB Type-C** FS (device/host, power up to 2.5 W)
- **STLINK-V3E** debugger/programmer with VCP

---

## 🗺 Bus Topology Summary

- **I²C2 (PH4/PH5)** → HTS221, IIS2MDCTR, ISM330DHCX, LPS22HH, VL53L5CX, VEML6030/3235, STSAFE, EEPROM
- **MDF/ADF** → Dual PDM microphones
- **SPI2** → Wi-Fi (EMW3080)
- **UART4** → Bluetooth LE (STM32WB5MMG)
- **Octo-SPI1/2** → Flash + PSRAM

---

## ✅ Quick Notes for Zephyr Development

- Most environmental and motion sensors sit on a **shared I²C2 bus**.
- Both MEMS microphones stream **PDM audio directly into STM32U5’s MDF/ADF interface**.
- No onboard audio codec or headphone jack.
- Wi-Fi uses **SPI2** (with AT command stack).
- Bluetooth is a **separate STM32WB module**, controlled via UART.
- STSAFE secure element and EEPROM also share I²C2.

---
