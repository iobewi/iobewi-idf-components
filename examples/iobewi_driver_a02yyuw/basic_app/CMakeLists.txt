cmake_minimum_required(VERSION 3.16)

# Tell ESP-IDF where to find the drv_a02yyuw component
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(a02yyuw_basic_app)
