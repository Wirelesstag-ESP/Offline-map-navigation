idf_component_register(
    SRCS "gps_lc76g.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_timer
)
