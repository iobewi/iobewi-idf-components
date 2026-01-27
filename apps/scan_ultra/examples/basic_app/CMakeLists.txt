cmake_minimum_required(VERSION 3.16)

# Tell ESP-IDF where to find the components
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../../drv_a02yyuw"
    "${CMAKE_CURRENT_LIST_DIR}/../../../lib_a02_provider"
    "${CMAKE_CURRENT_LIST_DIR}/../../../mw_uros_core"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_scan_ultra_basic_app)
