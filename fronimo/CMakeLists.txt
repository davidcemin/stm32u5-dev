# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fronimo)

target_sources(app PRIVATE  src/main.cpp
                            src/sensors/hts221.cpp
                            src/sensors/iis2mdc.cpp
)
