idf_component_register(SRCS "runtime_services.cpp" INCLUDE_DIRS "include"
    REQUIRES dashbridge_domain_core dashlink_v2)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
