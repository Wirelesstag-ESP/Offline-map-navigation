cmake_minimum_required(VERSION 3.16)
project(bno085_rvc_parser_tests LANGUAGES CXX)

enable_testing()

add_executable(bno085_rvc_parser_test
    test_bno085_rvc_parser.cpp
    ../bno085_rvc_parser.cpp
)
target_include_directories(bno085_rvc_parser_test PRIVATE ..)
target_compile_features(bno085_rvc_parser_test PRIVATE cxx_std_17)
target_compile_options(bno085_rvc_parser_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME bno085_rvc_parser COMMAND bno085_rvc_parser_test)
