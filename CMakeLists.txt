# Arcadia Toy SDK.
#
# Standalone build (SDK + samples, for development/CI):
#   cmake -B build -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-i686-w64-mingw32.cmake
#   cmake --build build
#
# As a dependency of your toy project (FetchContent):
#   include(FetchContent)
#   FetchContent_Declare(arcadia_sdk
#       GIT_REPOSITORY https://github.com/<you>/arcadia-toy-sdk.git
#       GIT_TAG        main)
#   FetchContent_MakeAvailable(arcadia_sdk)
#   add_arcadia_toy(mytoy NAME "MyToy" NUMBER 13 VERSION "0.0001" SOURCES mytoy.c)
# (Configure your project with the 32-bit toolchain; see cmake/toolchain-*.cmake.)
cmake_minimum_required(VERSION 3.16)
project(arcadia_toy_sdk C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ArcadiaToy)   # defines arcadia_toy_runtime + add_arcadia_toy()

# Build the bundled samples only when this SDK is the top-level project, so
# consumers pulling it in via FetchContent/add_subdirectory don't build them.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_arcadia_samples_default ON)
else()
    set(_arcadia_samples_default OFF)
endif()
option(ARCADIA_BUILD_SAMPLES "Build the sample toys" ${_arcadia_samples_default})
if(ARCADIA_BUILD_SAMPLES)
    add_subdirectory(samples/hello)
    add_subdirectory(samples/glcube)
endif()
