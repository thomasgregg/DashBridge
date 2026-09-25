if(CONFIG_BT_HFP_USE_EXTERNAL_CODEC)
    set(call_audio_source "call_audio_external.cpp")
else()
    set(call_audio_source "call_audio.cpp")
endif()

idf_component_register(SRCS "esp_hfp_adapter.cpp" "${call_audio_source}"
    INCLUDE_DIRS "include"
    REQUIRES calls_v3 dashbridge_adapter_api runtime_ports dashbridge_domain_core dashlink_v2 music_v1 bt nvs_flash esp_timer esp_driver_uart)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
