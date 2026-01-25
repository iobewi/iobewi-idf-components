cmake_minimum_required(VERSION 3.16)

# Ajouter le composant parent au chemin de recherche
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(app_scan_tof_basic_app)
