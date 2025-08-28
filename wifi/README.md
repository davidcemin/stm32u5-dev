
# STM32U585 WiFi Sample with EMW3080 Module

This sample application demonstrates WiFi connectivity using the EMW3080 WiFi module on an STM32U585 IoT Discovery board.

## Features

- WiFi station mode support (connect to existing networks)
- Network scanning to discover available access points
- Automatic DHCP configuration for IP address assignment
- DNS resolver support
- WiFi and network management shell commands
- Support for automatic connection to a pre-configured network

## Requirements

- Zephyr SDK and development environment
- West build tool configured for Zephyr
- STM32CubeProgrammer for flashing the board
- STM32U585 IoT Discovery board with EMW3080 WiFi module

## Hardware Setup

The EMW3080 module should be connected to the STM32U585 using UART4:

| STM32U585 Pin | Connection | EMW3080 Pin |
|---------------|------------|-------------|
| UART4 TX      | ↔          | UART RX     |
| UART4 RX      | ↔          | UART TX     |
| GPIO (RST)    | →          | RESET       |
| 3.3V          | →          | VCC         |
| GND           | →          | GND         |

## Building and Flashing

```bash
# Build the application
west build -b b_u585i_iot02a -p always

# Flash to the board
west flash
```

## Auto-Connect Feature

You can configure the sample to automatically connect to a WiFi network on startup by setting the following in prj.conf:

```
CONFIG_WIFI_AUTOCONNECT_SSID="YourWiFiSSID"
CONFIG_WIFI_AUTOCONNECT_PSK="YourWiFiPassword"
```

## WiFi Shell Commands

Once the application is running, you can use the following shell commands to control WiFi:

```
wifi scan                       # Scan for available networks
wifi connect <SSID> <password>  # Connect to a network
wifi status                     # Display current connection status
wifi disconnect                 # Disconnect from the current network
```

## Network Shell Commands

Additionally, you can use these network commands for diagnostics:

```
net iface                       # List network interfaces
net ipv4                        # Show IPv4 address information
net ping <host>                 # Ping a remote host
net dns <hostname>              # Resolve hostname to IP address
```

## Troubleshooting

If you encounter issues:

1. Check if the EMW3080 module is properly connected to the STM32U585
2. Verify that the UART4 pins are correctly configured in the device tree
3. Check the serial console for diagnostic messages
4. Run `device list` to verify that all required devices are registered
5. Use `net iface` to check network interface status
