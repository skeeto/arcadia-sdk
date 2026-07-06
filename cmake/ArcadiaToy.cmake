# ArcadiaToy.cmake — helpers to build and package an Arcadia toy.
#
# Provides:
#   add_arcadia_toy(<target> NAME ... VERSION ... SOURCES ... [options])
#
# and defines the imported `arcadia_toy_runtime` static library (the SDK shim
# that supplies the six MP* exports). Include this file after project().
#
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/sdk/cmake")
#   include(ArcadiaToy)

if(DEFINED _ARCADIA_TOY_CMAKE_INCLUDED)
    return()
endif()
set(_ARCADIA_TOY_CMAKE_INCLUDED TRUE)

# Root of the SDK (this file lives in <sdk>/cmake).
get_filename_component(ARCADIA_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ARCADIA_SDK_INCLUDE_DIR "${ARCADIA_SDK_DIR}/include")
set(ARCADIA_SDK_SRC_DIR     "${ARCADIA_SDK_DIR}/src")

# The SDK runtime, compiled once per project that uses it.
if(NOT TARGET arcadia_toy_runtime)
    add_library(arcadia_toy_runtime STATIC "${ARCADIA_SDK_SRC_DIR}/arcadia_toy.c")
    target_include_directories(arcadia_toy_runtime PUBLIC "${ARCADIA_SDK_INCLUDE_DIR}")
    set_target_properties(arcadia_toy_runtime PROPERTIES POSITION_INDEPENDENT_CODE OFF)
endif()

# add_arcadia_toy(<target>
#     NAME     "synFoo"          # display name  -> toy.ini name=   (required)
#     NUMBER   13                 # toy id; folder becomes toy<N>, N>0 (required)
#     VERSION  "0.0001"          # -> version.ini                    (required)
#     SOURCES  a.c b.c ...        # your toy sources                 (required)
#     DESC     "One-liner"        # -> toy.ini desc=
#     DLL      foo.dll            # output DLL filename (default <NAME>.dll)
#     PREVIEW  preview.jpg        # thumbnail, copied into the layout
#     LONGDESC "Longer text"      # -> toy.ini longdesc=
#     ASSETS   sfx art music      # extra files/dirs copied into the layout
#     STAGE_DIR <dir>             # where to assemble the toy folder
#                                 #   (default ${CMAKE_BINARY_DIR}/toys/toy<N>)
# )
# Arcadia finds a toy by scanning Toys\ and requires the folder to be named
# toy<N> (atoi of the name after the first 3 chars must be > 0). N is the toy's
# identity/registry slot; use N >= 13 (1..12 are the official toys).
function(add_arcadia_toy target)
    cmake_parse_arguments(AT
        ""                                  # options
        "NAME;VERSION;NUMBER;DESC;DLL;PREVIEW;LONGDESC;STAGE_DIR"  # one-value
        "SOURCES;ASSETS"                    # multi-value
        ${ARGN})

    if(NOT AT_NAME)
        message(FATAL_ERROR "add_arcadia_toy(${target}): NAME is required")
    endif()
    if(NOT AT_VERSION)
        message(FATAL_ERROR "add_arcadia_toy(${target}): VERSION is required")
    endif()
    if(NOT AT_SOURCES)
        message(FATAL_ERROR "add_arcadia_toy(${target}): SOURCES is required")
    endif()
    # Arcadia discovers a toy by scanning Toys\ and computing atoi(foldername+3):
    # the folder MUST be named toy<N> with N a positive integer, and N becomes
    # the toy's identity (its HKCU ...\TOYS\TOY<N> registry namespace). The 12
    # official toys occupy 1..12, so third-party toys should use N >= 13.
    if(NOT AT_NUMBER)
        message(FATAL_ERROR
            "add_arcadia_toy(${target}): NUMBER is required. Arcadia only loads "
            "toys from folders named toy<N> (N>0); pick an unused N>=13.")
    endif()
    if(NOT AT_NUMBER MATCHES "^[0-9]+$" OR AT_NUMBER LESS 1)
        message(FATAL_ERROR "add_arcadia_toy(${target}): NUMBER must be a positive integer")
    endif()
    set(AT_FOLDER "toy${AT_NUMBER}")
    if(NOT AT_DLL)
        set(AT_DLL "${AT_NAME}.dll")
    endif()
    if(NOT AT_DESC)
        set(AT_DESC "${AT_NAME}")
    endif()
    if(NOT AT_STAGE_DIR)
        set(AT_STAGE_DIR "${CMAKE_BINARY_DIR}/toys/${AT_FOLDER}")
    endif()

    # The toy DLL: your sources + the SDK runtime + the export list. Sources may
    # be C or C++ — arcadia/toy.h is wrapped in extern "C", so a C++ toy's
    # ArcadiaToyRegister() and its ar_* references keep C linkage. (Enable the
    # CXX language in your project() to build .cpp toys.)
    add_library(${target} SHARED ${AT_SOURCES})
    target_link_libraries(${target} PRIVATE arcadia_toy_runtime)
    target_include_directories(${target} PRIVATE "${ARCADIA_SDK_INCLUDE_DIR}")
    target_link_libraries(${target} PRIVATE gdi32 user32 winmm)

    # Keep C++ toys self-contained: static-link libstdc++/libgcc so the DLL has
    # no external runtime-DLL dependency. w32devkit links these statically
    # already; MSYS2 and other mingw default to dynamic libstdc++, which would
    # force shipping libstdc++-6.dll alongside the toy. Harmless no-op for a
    # pure-C toy. (MSVC: set CMAKE_MSVC_RUNTIME_LIBRARY to "MultiThreaded" in
    # your project for a redist-free DLL.)
    if(NOT MSVC)
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
    endif()

    # Output must be named exactly as toy.ini dll= (no "lib" prefix, .dll suffix).
    get_filename_component(_dllbase "${AT_DLL}" NAME_WE)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${_dllbase}"
        SUFFIX ".dll")

    # Apply the module-definition file so the six MP* names export undecorated.
    # GNU ld and MSVC both accept a .def passed on the link line.
    set(_def "${ARCADIA_SDK_SRC_DIR}/arcadia_toy.def")
    if(MSVC)
        target_link_options(${target} PRIVATE "/DEF:${_def}")
    else()
        target_link_options(${target} PRIVATE "-Wl,--enable-stdcall-fixup" "${_def}")
    endif()
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_def}")

    # ---- generate toy.ini / version.ini into the stage dir --------------
    set(ARTOY_NAME    "${AT_NAME}")
    set(ARTOY_DESC    "${AT_DESC}")
    set(ARTOY_DLL     "${AT_DLL}")
    set(ARTOY_VERSION "${AT_VERSION}")
    if(AT_PREVIEW)
        get_filename_component(ARTOY_PREVIEW "${AT_PREVIEW}" NAME)
    else()
        set(ARTOY_PREVIEW "")
    endif()
    if(AT_LONGDESC)
        set(ARTOY_LONGDESC_LINE "longdesc=${AT_LONGDESC}")
    else()
        set(ARTOY_LONGDESC_LINE "")
    endif()

    file(MAKE_DIRECTORY "${AT_STAGE_DIR}")
    configure_file("${ARCADIA_SDK_DIR}/cmake/toy.ini.in"
                   "${AT_STAGE_DIR}/toy.ini"     @ONLY)
    configure_file("${ARCADIA_SDK_DIR}/cmake/version.ini.in"
                   "${AT_STAGE_DIR}/version.ini" @ONLY)

    # ---- assemble the layout after build --------------------------------
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AT_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${target}>" "${AT_STAGE_DIR}/${AT_DLL}"
        COMMENT "Staging Arcadia toy '${AT_NAME}' -> ${AT_STAGE_DIR}")

    if(AT_PREVIEW)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${AT_PREVIEW}" "${AT_STAGE_DIR}/${ARTOY_PREVIEW}")
    endif()

    foreach(asset ${AT_ASSETS})
        if(IS_DIRECTORY "${asset}")
            get_filename_component(_an "${asset}" NAME)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${asset}" "${AT_STAGE_DIR}/${_an}")
        else()
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${asset}" "${AT_STAGE_DIR}/")
        endif()
    endforeach()

    # Expose the stage dir / folder name and an install rule.
    set_target_properties(${target} PROPERTIES
        ARCADIA_STAGE_DIR "${AT_STAGE_DIR}"
        ARCADIA_FOLDER    "${AT_FOLDER}")

    # `cmake --install` (or `make install`) drops the toy under
    # <prefix>/Toys/toy<N>/ ready to copy into an Arcadia install.
    install(DIRECTORY "${AT_STAGE_DIR}/" DESTINATION "Toys/${AT_FOLDER}")

    # ---- ready-to-publish zip -------------------------------------------
    #   cmake --build <build> --target <target>_package   ->   <build>/toy<N>.zip
    # Bundles the staged toy<N>/ folder into a standard ZIP (Arcadia's own
    # SRNZipExtract reads PK\x03\x04 archives) with a single top-level toy<N>/
    # directory, so a user unzips it straight into their Arcadia\Toys\ folder.
    # Built with cmake -E tar (bundled libarchive) so no external zip tool is
    # needed. A per-toy custom target rather than CPack, which is project-global
    # and packages the install tree — it can't easily emit a precisely-named
    # toy<N>.zip with this exact layout.
    get_filename_component(_toys_dir "${AT_STAGE_DIR}" DIRECTORY)
    add_custom_target(${target}_package
        COMMAND ${CMAKE_COMMAND} -E tar cf
                "${CMAKE_BINARY_DIR}/${AT_FOLDER}.zip" --format=zip "${AT_FOLDER}"
        WORKING_DIRECTORY "${_toys_dir}"
        COMMENT "Packaging ${AT_FOLDER}.zip (unzip into Arcadia\\Toys\\)"
        VERBATIM)
    add_dependencies(${target}_package ${target})
endfunction()

# install_arcadia_toy_to(<target> <ArcadiaDir>)
#   Convenience: copy a staged toy straight into a real Arcadia install's
#   Toys/toy<N> folder at build time (handy for local iteration).
function(install_arcadia_toy_to target arcadia_dir)
    get_target_property(_stage  ${target} ARCADIA_STAGE_DIR)
    get_target_property(_folder ${target} ARCADIA_FOLDER)
    add_custom_target(${target}_deploy
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_stage}" "${arcadia_dir}/Toys/${_folder}"
        DEPENDS ${target}
        COMMENT "Deploying '${target}' into ${arcadia_dir}/Toys/${_folder}")
endfunction()
