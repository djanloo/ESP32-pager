cmake_minimum_required(VERSION 3.16)

# Credenziali e nome del dispositivo stanno in sdkconfig.local (non versionato).
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cercapersone)
