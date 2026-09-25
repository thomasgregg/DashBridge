idf_component_register(SRCS "esp_ancs_adapter.cpp" "ancs_codec.cpp" INCLUDE_DIRS "include"
    REQUIRES dashbridge_adapter_api runtime_ports dashbridge_domain_core dashlink_v2 bt nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
