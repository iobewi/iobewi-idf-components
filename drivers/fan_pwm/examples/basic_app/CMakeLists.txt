cmake_minimum_required(VERSION 3.16)

# On ajoute le composant parent (drv_fan_pwm) dans les composants visibles du projet
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(drv_fan_pwm_basic_app)
