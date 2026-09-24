idf_component_register(SRCS "esp_tesla_adapter.cpp"
    REQUIRES dashbridge_adapter_api runtime_ports dashbridge_domain_core dashlink_v2 obex map_adapter pbap_adapter bt)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
