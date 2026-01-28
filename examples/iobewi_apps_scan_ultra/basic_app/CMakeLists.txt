cmake_minimum_required(VERSION 3.16)

# Tell ESP-IDF where to find the components
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(app_scan_ultra_basic_app)
