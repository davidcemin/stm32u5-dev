# ST Sensor Drivers CMakeLists.txt

# Add VL53L5CX driver
add_subdirectory_ifdef(CONFIG_VL53L5CX vl53l5cx)
