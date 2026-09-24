idf_component_register(SRCS "setup_gatt_v1.cpp" INCLUDE_DIRS "include" REQUIRES dashbridge_domain_core)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
