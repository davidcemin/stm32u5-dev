# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fronimo)

# Add custom drivers
add_subdirectory(drivers)

target_sources(app PRIVATE  src/main.cpp
                            src/sensors/hts221.cpp
                            src/sensors/iis2mdc.cpp
                            src/sensors/ism330dhcx.cpp
                            src/sensors/lps22hh.cpp
                            src/sensors/vl53l5cx.cpp
)
