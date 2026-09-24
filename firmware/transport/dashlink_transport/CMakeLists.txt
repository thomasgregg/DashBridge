idf_component_register(SRCS "dashlink_transport.cpp" INCLUDE_DIRS "include"
    REQUIRES dashlink_v2)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
