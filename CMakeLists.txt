cmake_minimum_required(VERSION 3.30)
project(MQTT_Benchmark VERSION 1.0)

# Compiler settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "/usr/local/lib;/opt/homebrew/lib")  # Added Homebrew path for Apple Silicon

# Include directories (for both Paho and Mosquitto)
include_directories(
        /usr/local/include
        /opt/homebrew/include  # Homebrew (Apple Silicon)
)

# --- Paho MQTT Libraries ---
find_library(PAHO_MQTTPP3_LIBRARY paho-mqttpp3 PATHS /usr/local/lib /opt/homebrew/lib)
find_library(PAHO_MQTT3C_LIBRARY paho-mqtt3c PATHS /usr/local/lib /opt/homebrew/lib)

if(NOT PAHO_MQTTPP3_LIBRARY)
    message(FATAL_ERROR "paho-mqttpp3 library not found")
endif()

if(NOT PAHO_MQTT3C_LIBRARY)
    message(FATAL_ERROR "paho-mqtt3c library not found")
endif()

# --- Mosquitto Library ---
find_library(MOSQUITTO_LIB mosquitto PATHS /usr/local/lib /opt/homebrew/lib)
if(NOT MOSQUITTO_LIB)
    message(FATAL_ERROR "mosquitto library not found")
endif()

# Executables
add_executable(consumer consumer.cpp)
add_executable(producer producer.cpp)

# Link libraries
target_link_libraries(consumer PRIVATE
        ${PAHO_MQTTPP3_LIBRARY}
        ${PAHO_MQTT3C_LIBRARY}
        ${MOSQUITTO_LIB}  # Link Mosquitto
)

target_link_libraries(producer PRIVATE
        ${PAHO_MQTTPP3_LIBRARY}
        ${PAHO_MQTT3C_LIBRARY}
        ${MOSQUITTO_LIB}  # Link Mosquitto
)

# RPATH settings
set_target_properties(consumer PROPERTIES INSTALL_RPATH "/usr/local/lib;/opt/homebrew/lib")
set_target_properties(producer PROPERTIES INSTALL_RPATH "/usr/local/lib;/opt/homebrew/lib")

# Compiler warnings
target_compile_options(consumer PRIVATE -Wall)
target_compile_options(producer PRIVATE -Wall)