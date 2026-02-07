cmake_minimum_required(VERSION 3.16)

# Ajouter le composant parent au chemin de recherche
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(drv_max98357a_audio_tone_app)
