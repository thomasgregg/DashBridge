idf_component_register(SRCS "main.cpp" "phone.cpp" "car.cpp" INCLUDE_DIRS "." REQUIRES bridge_core bt nvs_flash esp_driver_uart esp_driver_gpio esp_timer)
