idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash spiffs wt99p4c5_s1_board gps_lc76g bno085_attitude lvgl simple_map
)
