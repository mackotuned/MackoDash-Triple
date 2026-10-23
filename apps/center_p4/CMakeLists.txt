cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32p4" CACHE STRING "ESP-IDF target")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components"
                         "${CMAKE_CURRENT_LIST_DIR}/../../private/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.1.0")
project(mackodash_triple_center)
