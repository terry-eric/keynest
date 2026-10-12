cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
# PlatformIO needs its generated language probe component in filtered builds.
if(EXISTS "$ENV{IDF_PATH}/components/__pio_env/CMakeLists.txt")
    list(APPEND COMPONENTS __pio_env)
endif()
project(pcb2_s3_board_demo)
