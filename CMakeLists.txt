cmake_minimum_required(VERSION 3.16)

project(uploadNVR VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

# ------------------------------------------------------------------------------
# System dependencies
# ------------------------------------------------------------------------------

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Required for parsing config.yaml
find_package(yaml-cpp REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
)

pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

# ------------------------------------------------------------------------------
# TensorFlow source
# ------------------------------------------------------------------------------

set(TENSORFLOW_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tensorflow"
        CACHE PATH "TensorFlow source directory" FORCE
)

# ------------------------------------------------------------------------------
# FlatBuffers
# TensorFlow 2.21.0 checkout requires FlatBuffers 25.9.x
# ------------------------------------------------------------------------------

include(FetchContent)

set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_GRPCTEST OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG v25.9.23
)

FetchContent_MakeAvailable(flatbuffers)

# ------------------------------------------------------------------------------
# TensorFlow Lite
# ------------------------------------------------------------------------------

set(TFLITE_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TFLITE_ENABLE_XNNPACK OFF CACHE BOOL "" FORCE)

add_subdirectory(
        "${TENSORFLOW_SOURCE_DIR}/tensorflow/lite"
        "${CMAKE_CURRENT_BINARY_DIR}/tensorflow-lite"
        EXCLUDE_FROM_ALL
)

# ------------------------------------------------------------------------------
# Mandatory Google Coral Edge TPU
# ------------------------------------------------------------------------------

find_library(
        EDGETPU_LIB
        NAMES edgetpu
        HINTS
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
        /usr/local/lib
        /opt/homebrew/lib
)

if(NOT EDGETPU_LIB)
    message(FATAL_ERROR "Google Coral Edge TPU library (libedgetpu) is REQUIRED but was not found. Build aborted.")
endif()

message(STATUS "Edge TPU library found: ${EDGETPU_LIB}")
set(AI_LIBRARIES tensorflow-lite ${EDGETPU_LIB})

# ------------------------------------------------------------------------------
# Application
# ------------------------------------------------------------------------------

set(SOURCES
        src/main.cpp
        src/CameraNode.cpp
        src/InferenceEngine.cpp
        src/Encryptor.cpp
        src/CloudSync.cpp
        src/Decryptor.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# ------------------------------------------------------------------------------
# Include directories
# ------------------------------------------------------------------------------

target_include_directories(${PROJECT_NAME} PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${TENSORFLOW_SOURCE_DIR}"
        ${FFMPEG_INCLUDE_DIRS}
        ${SODIUM_INCLUDE_DIRS}
)

# ------------------------------------------------------------------------------
# Linking
# ------------------------------------------------------------------------------

target_link_libraries(${PROJECT_NAME} PRIVATE
        Threads::Threads
        PkgConfig::FFMPEG
        PkgConfig::SODIUM
        yaml-cpp
        ${AI_LIBRARIES}
)