# Build the "hello" sample toy. This doubles as the template for your own toy:
# copy this folder, rename, and edit the add_arcadia_toy() call.
cmake_minimum_required(VERSION 3.16)
project(hello_toy C)

# Find the SDK's CMake helpers. When you copy this out of the SDK tree, set
# ARCADIA_SDK_DIR (or adjust this path) to point at the sdk/ folder.
if(NOT DEFINED ARCADIA_SDK_CMAKE)
    set(ARCADIA_SDK_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
endif()
list(APPEND CMAKE_MODULE_PATH "${ARCADIA_SDK_CMAKE}")
include(ArcadiaToy)

add_arcadia_toy(hello
    NAME     "Hello"
    NUMBER   13                  # Arcadia loads Toys\toy<N>; N>=13 avoids the 12 official toys
    DESC     "SDK sample toy"
    VERSION  "0.0001"
    SOURCES  hello.c
    LONGDESC "A bouncing-ball hello world built with the Arcadia Toy SDK."
    # PREVIEW hello.jpg          # (optional) 120x90-ish thumbnail
    # ASSETS  sfx art            # (optional) folders copied into the layout
)

# Optional: deploy straight into a local Arcadia install for testing, e.g.
#   cmake --build build --target hello_deploy
# install_arcadia_toy_to(hello "C:/Users/you/desktop/Arcadia")
