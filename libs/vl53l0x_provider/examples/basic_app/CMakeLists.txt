cmake_minimum_required(VERSION 3.16)

# Charger uniquement lib_vl53l0x_provider (et ses dépendances)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lib_vl53l0x_provider_basic_app)
