# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Static dependencies of the FFmpeg portlib; they must follow it on the link line.
string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -ldav1d -lz")

add_executable(eden-horizon
    main.cpp
    platform.cpp
    platform.h
    profiler.cpp
    profiler.h
    status_overlay.cpp
    status_overlay.h
    version.h)

target_link_libraries(eden-horizon PRIVATE common core hid_core input_common frontend_common video_core)
target_link_libraries(eden-horizon PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# OunceVK (Vulkan driver, static) and kaiHEN's JIT provider for libounce's Jit API.
# libnx (-lnx) comes from the toolchain's standard libraries.
target_link_libraries(eden-horizon PRIVATE vulkan kaihen)



# needed for vma
target_compile_options(eden-horizon PRIVATE
    -Wno-conversion -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers)

nx_generate_nacp(
    OUTPUT eden.nacp
    NAME "Eden"
    AUTHOR "Eden Emulator Project"
    VERSION "0.0.0-poc")

nx_create_nro(eden-horizon
    OUTPUT ${CMAKE_BINARY_DIR}/bin/eden.nro
    NACP eden.nacp)
