# Custom Drivers CMakeLists.txt

# Add sensor drivers
add_subdirectory_ifdef(CONFIG_SENSOR sensor)

# Add audio drivers
add_subdirectory_ifdef(CONFIG_AUDIO audio)
