# Custom Drivers CMakeLists.txt

# Add sensor drivers
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
