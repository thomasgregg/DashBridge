idf_component_register(
    SRCS "setup_gatt_v1_adapter.cpp"
    INCLUDE_DIRS "include"
    REQUIRES dashbridge_domain_core setup_gatt_v1 bt
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
