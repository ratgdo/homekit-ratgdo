cmake_minimum_required(VERSION 3.5)

if(DEFINED ENV{HOMEKIT_PATH})
  set(HOMEKIT_PATH $ENV{HOMEKIT_PATH})
else()
  set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/../..)
endif(DEFINED ENV{HOMEKIT_PATH})

target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE")

set(EXTRA_COMPONENT_DIRS ${HOMEKIT_PATH}/components ${HOMEKIT_PATH}/components/homekit)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ratgdo)
