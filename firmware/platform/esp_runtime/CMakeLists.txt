idf_component_register(SRCS "esp_runtime.cpp" INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio esp_timer esp_app_format)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
