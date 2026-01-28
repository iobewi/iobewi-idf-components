cmake_minimum_required(VERSION 3.16)

# Ajouter les composants nécessaires au chemin de recherche
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(app_scan_tof_basic_app)
