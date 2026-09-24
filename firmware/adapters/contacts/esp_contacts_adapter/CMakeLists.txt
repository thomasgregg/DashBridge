idf_component_register(SRCS "esp_contacts_adapter.cpp"
    REQUIRES contacts_v1 dashbridge_adapter_api runtime_ports dashbridge_domain_core dashlink_v2 bt esp_timer)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
