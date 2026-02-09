cmake_minimum_required(VERSION 3.16)

# Ajouter le composant driver MAX98357A
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/iobewi_driver_max98357a"
)

# Utiliser la table de partitions personnalisée
set(PARTITION_TABLE_CSV_PATH "${CMAKE_CURRENT_LIST_DIR}/partitions.csv")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(drv_max98357a_hls_stream_app)
