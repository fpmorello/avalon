# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Static dependencies of the FFmpeg portlib; they must follow it on the link line.
string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -ldav1d -lz")

add_executable(eden-horizon
    main.cpp
    pagetable_selftest.cpp
    pagetable_selftest.h
    platform.cpp
    platform.h
    profiler.cpp
    profiler.h
    ui_image.cpp
    ui_image.h
    ui_render.cpp
    ui_render.h
    ui_text.h
    status_overlay.cpp
    status_overlay.h
    version.h)

target_link_libraries(eden-horizon PRIVATE common core hid_core input_common frontend_common video_core)
# The page table self-test drives dynarmic directly.
target_link_libraries(eden-horizon PRIVATE dynarmic::dynarmic)
target_link_libraries(eden-horizon PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# OunceVK (Vulkan driver, static) and kaiHEN's JIT provider for libounce's Jit API.
# libnx (-lnx) comes from the toolchain's standard libraries.
target_link_libraries(eden-horizon PRIVATE vulkan kaihen)



# needed for vma
target_compile_options(eden-horizon PRIVATE
    -Wno-conversion -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers)

nx_generate_nacp(
    OUTPUT avalon.nacp
    NAME "Avalon"
    AUTHOR "Eden Emulator Project"
    VERSION "0.0.0-poc")

nx_create_nro(eden-horizon
    OUTPUT ${CMAKE_BINARY_DIR}/bin/avalon.nro
    ICON ${CMAKE_CURRENT_SOURCE_DIR}/ui_assets/icon.jpg
    NACP avalon.nacp)

# ui_render.cpp embeds the generated assets (#embed): rebuild it when they change.
set_property(SOURCE ui_render.cpp APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_assets/font_small.bin
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_assets/font_body.bin
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_assets/font_title.bin
    ${CMAKE_CURRENT_SOURCE_DIR}/ui_assets/logo.bin)
