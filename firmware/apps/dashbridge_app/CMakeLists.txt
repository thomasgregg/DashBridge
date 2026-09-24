idf_component_register(SRCS "app_main.cpp" "setup_composition.cpp" INCLUDE_DIRS "."
    REQUIRES dashbridge_adapter_api runtime_ports dashbridge_domain_core
             dashlink_v2 dashlink_transport esp_runtime esp_ancs_adapter esp_tesla_adapter
             esp_hfp_adapter esp_contacts_adapter esp_a2dp_avrcp_adapter setup_gatt_v1_adapter)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
