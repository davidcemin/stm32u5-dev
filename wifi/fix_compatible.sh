#!/bin/sh
# Script to fix the EMW3080 driver and device tree binding

echo "Updating device tree overlay compatible string..."
sed -i 's/compatible = "mxchip,emw3080"/compatible = "mxchip,emw3080" # Match with DT_DRV_COMPAT=mxchip_emw3080/g' boards/b_u585i_iot02a.overlay

echo "Ensuring DT_DRV_COMPAT matches in emw3080_debug.c..."
sed -i 's/#define DT_DRV_COMPAT mxchip_emw3080/#define DT_DRV_COMPAT mxchip_emw3080/g' drivers/wifi/emw3080/emw3080_debug.c

echo "Patch applied successfully!"
