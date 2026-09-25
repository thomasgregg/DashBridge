idf_component_register(SRCS "calls.cpp" "connections.cpp" "contacts.cpp" "messages.cpp" "music.cpp" "setup.cpp" "text.cpp"
                       INCLUDE_DIRS "include")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
