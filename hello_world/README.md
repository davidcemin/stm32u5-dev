
# Zephyr Hello World

## Requirements

- Install west and everything needed for Zephyr
- Source the zephyr-env.sh script from the top directory of this repo
- Install STM32Cube-CLT to get the CubeProgrammer CommandLine application


## Build and Flash

```
west build -b b_u585i_iot02a  -p always
west flash
```
