# Toolchain file for building Arcadia toys as 32-bit Windows DLLs with the
# i686-w64-mingw32 (w32devkit) GCC. Arcadia.exe and every shipped toy are
# 32-bit (pei-i386), so toys MUST be 32-bit too.
#
# Usage:
#   cmake -B build -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=/path/to/sdk/cmake/toolchain-i686-w64-mingw32.cmake
#
# Point ARCADIA_MINGW_PREFIX at your toolchain's bin dir if it is not on PATH.
# On this machine the w32 w64devkit lives at ~/w32devkit.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

# Locate the cross compilers. Prefer an explicit prefix, then a bare name on PATH.
if(NOT DEFINED ARCADIA_MINGW_PREFIX)
    if(DEFINED ENV{ARCADIA_MINGW_PREFIX})
        set(ARCADIA_MINGW_PREFIX "$ENV{ARCADIA_MINGW_PREFIX}")
    elseif(EXISTS "$ENV{HOME}/w32devkit/bin/gcc.exe")
        set(ARCADIA_MINGW_PREFIX "$ENV{HOME}/w32devkit/bin")
    elseif(EXISTS "$ENV{USERPROFILE}/w32devkit/bin/gcc.exe")
        set(ARCADIA_MINGW_PREFIX "$ENV{USERPROFILE}/w32devkit/bin")
    endif()
endif()

# w64devkit ships bare gcc/g++ (already i686). A classic cross toolchain uses the
# i686-w64-mingw32- prefix. Support both.
find_program(CMAKE_C_COMPILER
    NAMES i686-w64-mingw32-gcc gcc
    HINTS ${ARCADIA_MINGW_PREFIX})
find_program(CMAKE_CXX_COMPILER
    NAMES i686-w64-mingw32-g++ g++ c++
    HINTS ${ARCADIA_MINGW_PREFIX})
find_program(CMAKE_RC_COMPILER
    NAMES i686-w64-mingw32-windres windres
    HINTS ${ARCADIA_MINGW_PREFIX})

# Force 32-bit even if the compiler defaults elsewhere.
set(_ar_m32 "-m32")
set(CMAKE_C_FLAGS_INIT   "${_ar_m32}")
set(CMAKE_CXX_FLAGS_INIT "${_ar_m32}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
