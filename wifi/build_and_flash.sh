#!/bin/sh
# Script to build and flash the EMW3080 WiFi driver sample

echo "Starting build process..."

# Navigate to the project directory
cd /Users/david/proj/st/stm32u5-dev/wifi

# Build with west (assuming Zephyr environment is set up)
echo "Building firmware..."
west build -b b_u585i_iot02a

# Flash the board (assuming OpenOCD is properly set up)
echo "Flashing firmware..."
west flash

echo "Build and flash complete!"
