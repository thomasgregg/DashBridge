idf_component_register(SRCS "map_adapter.cpp" INCLUDE_DIRS "include"
                       REQUIRES dashbridge_domain_core obex)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
