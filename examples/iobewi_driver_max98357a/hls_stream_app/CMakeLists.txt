cmake_minimum_required(VERSION 3.16)

# Ajouter les composants nécessaires
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/iobewi_driver_max98357a"
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/iobewi_apps_hls_player"
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/iobewi_libs_m3u8_parser"
)

# Utiliser la table de partitions personnalisée
set(PARTITION_TABLE_CSV_PATH "${CMAKE_CURRENT_LIST_DIR}/partitions.csv")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(drv_max98357a_hls_stream_app)
