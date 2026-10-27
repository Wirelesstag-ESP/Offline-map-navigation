idf_component_register(
    SRCS "bno085_attitude.cpp" "bno085_rvc_parser.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_driver_uart esp_timer freertos
)
