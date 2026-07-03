# Build the "glcube" sample: a hardware-accelerated OpenGL cube in a child
# window over the Arcadia canvas. Shows the GL-window approach from docs/ABI.md.
cmake_minimum_required(VERSION 3.16)
project(glcube_toy C)

if(NOT DEFINED ARCADIA_SDK_CMAKE)
    set(ARCADIA_SDK_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
endif()
list(APPEND CMAKE_MODULE_PATH "${ARCADIA_SDK_CMAKE}")
include(ArcadiaToy)

add_arcadia_toy(glcube
    NAME     "GL Cube"
    NUMBER   14                  # its own toy folder (toy14); 1-12 are official
    DLL      glcube.dll          # avoid a space in the DLL filename
    DESC     "Spinning OpenGL cube (drag to rotate)"
    VERSION  "0.0001"
    SOURCES  glcube.c
    LONGDESC "A hardware-accelerated OpenGL cube in a child window over the Arcadia canvas."
)
target_link_libraries(glcube PRIVATE opengl32 winmm)
