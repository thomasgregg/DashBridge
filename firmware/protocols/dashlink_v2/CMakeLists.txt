idf_component_register(SRCS "dashlink_v2.cpp" INCLUDE_DIRS "include"
                       REQUIRES dashbridge_domain_core)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
