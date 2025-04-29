cmake_minimum_required(VERSION 3.20)
project(MQTT_Benchmark VERSION 1.0)

## Find the compiler version
#if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
#    message(STATUS "Detected Clang compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
#
#    # Parse the major version (before the first dot)
#    string(REGEX MATCH "^[0-9]+" CLANG_VERSION_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
#    message(STATUS "Detected Clang major version: ${CLANG_VERSION_MAJOR}")
#
#    if(NOT CLANG_VERSION_MAJOR EQUAL 18)
#        message(FATAL_ERROR "Clang incorrect version (${CLANG_VERSION_MAJOR}), please use only Clang 18!")
#    endif()
#endif()


if(NOT DEFINED ENV{PAHO_INC_PATH})
    message(FATAL_ERROR "Environment variable PAHO_INC_PATH is not set!")
endif()

if(NOT DEFINED ENV{MOSQUITTO_INC_PATH})
    message(FATAL_ERROR "Environment variable MOSQUITTO_INC_PATH is not set!")
endif()

if(NOT DEFINED ENV{PAHO_LIB_PATH})
    message(FATAL_ERROR "Environment variable PAHO_LIB_PATH is not set!")
endif()

if(NOT DEFINED ENV{MOSQUITTO_LIB_PATH})
    message(FATAL_ERROR "Environment variable MOSQUITTO_LIB_PATH is not set!")
endif()

# Compiler settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories (for both Paho and Mosquitto)
include_directories(
        $ENV{PAHO_INC_PATH}
        $ENV{MOSQUITTO_INC_PATH}
)

# --- Paho MQTT Libraries ---
find_library(PAHO_MQTTPP3_LIBRARY paho-mqttpp3 PATHS $ENV{PAHO_LIB_PATH})
find_library(PAHO_MQTT3C_LIBRARY paho-mqtt3c PATHS $ENV{PAHO_LIB_PATH})

if(NOT PAHO_MQTTPP3_LIBRARY)
    message(FATAL_ERROR "paho-mqttpp3 library not found")
endif()

if(NOT PAHO_MQTT3C_LIBRARY)
    message(FATAL_ERROR "paho-mqtt3c library not found")
endif()

# --- Mosquitto Library ---
find_library(MOSQUITTO_LIB mosquitto PATHS $ENV{MOSQUITTO_LIB_PATH})
if(NOT MOSQUITTO_LIB)
    message(FATAL_ERROR "mosquitto library not found")
endif()

# Executables
add_executable(consume consumer.cpp)
add_executable(produce producer.cpp)

# Link libraries
target_link_libraries(consume PRIVATE
        ${PAHO_MQTTPP3_LIBRARY}
        ${PAHO_MQTT3C_LIBRARY}
        ${MOSQUITTO_LIB}
)

target_link_libraries(produce PRIVATE
        ${PAHO_MQTTPP3_LIBRARY}
        ${PAHO_MQTT3C_LIBRARY}
        ${MOSQUITTO_LIB}
)

# Compiler warnings
target_compile_options(consume PRIVATE -Wall -Wextra)
target_compile_options(produce PRIVATE -Wall -Wextra)