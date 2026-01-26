cmake_minimum_required(VERSION 3.16)

# Tell ESP-IDF where to find additional components.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(a02yyuw_multi_app)
