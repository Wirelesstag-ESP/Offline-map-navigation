cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(
    -Wno-format
    -Wno-ignored-qualifiers
    -Wno-missing-field-initializers
)

project(simple_map_wt99p4c5_s1)

set(MAP_STORAGE_DIR "${CMAKE_CURRENT_LIST_DIR}/storage_data")
if(EXISTS "${MAP_STORAGE_DIR}/tiles1")
    spiffs_create_partition_image(storage "${MAP_STORAGE_DIR}" FLASH_IN_PROJECT)
else()
    message(WARNING
        "No internal map data found at ${MAP_STORAGE_DIR}/tiles1; "
        "the firmware will require an SD card")
endif()
