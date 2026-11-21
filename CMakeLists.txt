# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)
project(pst_forever LANGUAGES C CXX ASM_MASM)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "x64 only: configure with -A x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no redist needed
add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# Pure logic (no Windows, no engine): shared by the DLL and the unit tests.
add_library(pstf_logic STATIC
  src/core/ini.cpp
  src/core/patch_math.cpp
)
target_include_directories(pstf_logic PUBLIC src)

# version.dll proxy loader
add_library(version_proxy SHARED
  src/loader/version_proxy.cpp
  src/loader/version_jumps.asm
  src/loader/version.def
)
set_target_properties(version_proxy PROPERTIES OUTPUT_NAME version)
target_link_libraries(version_proxy PRIVATE pstf_logic)

# core + modules
add_library(pst_forever SHARED
  src/core/init.cpp
  src/core/log.cpp
  src/core/config.cpp
  src/core/version_guard.cpp
  src/core/patch.cpp
  src/engine/sound.cpp
  src/modules/voice/examine_voice.cpp
)
target_link_libraries(pst_forever PRIVATE pstf_logic bcrypt)

# L0 unit tests
add_executable(pstf_tests tests/unit/test_main.cpp tests/unit/test_logic.cpp)
target_link_libraries(pstf_tests PRIVATE pstf_logic)
target_include_directories(pstf_tests PRIVATE tests/unit)

# L0 static hook verification against the on-disk Torment.exe
add_executable(pstf_verify tools/pstf_verify.cpp)
target_link_libraries(pstf_verify PRIVATE pstf_logic bcrypt)
