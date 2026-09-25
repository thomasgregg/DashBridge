idf_component_register(SRCS "esp_a2dp_avrcp_adapter.cpp"
    PRIV_INCLUDE_DIRS "."
    REQUIRES dashbridge_adapter_api runtime_ports dashbridge_domain_core dashlink_v2 music_v1 bt esp_timer)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
