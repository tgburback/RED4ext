add_library(RED4ext.Dll SHARED)

set_target_properties(RED4ext.Dll PROPERTIES OUTPUT_NAME RED4ext)

configure_file(Version.hpp.in "${CMAKE_CURRENT_SOURCE_DIR}/Version.hpp" @ONLY)

file(GLOB_RECURSE HEADER_FILES *.hpp)
file(GLOB_RECURSE SOURCE_FILES *.cpp)
file(GLOB_RECURSE RC_FILES *.rc)

source_group(_CMake REGULAR_EXPRESSION cmake_pch.*)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HEADER_FILES} ${SOURCE_FILES} ${RC_FILES})

target_include_directories(RED4ext.Dll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(RED4ext.Dll PRIVATE ${HEADER_FILES} ${SOURCE_FILES} ${RC_FILES})

# Compile Utils.cpp with /EHa so catch(...) catches SEH exceptions as well as
# C++ exceptions. Used by a Wine/Proton workaround in Utils::CreateLogger where
# spdlog's registry mutex init can raise a Win32 access violation. See the
# comment in Utils.cpp for details.
#
# Suppress C4652 (exception-handling option inconsistent with PCH) because the
# shared PCH is compiled with /EHsc; the inconsistency is intentional and
# localised to this file.
if (MSVC)
  set_source_files_properties(Utils.cpp PROPERTIES COMPILE_OPTIONS "/EHa;/wd4652")
endif ()

target_precompile_headers(RED4ext.Dll PUBLIC stdafx.hpp)
target_link_libraries(RED4ext.Dll
  PUBLIC
    version

    Detours
    fmt
    RED4ext::SDK
    simdjson::simdjson
    spdlog
    toml11
    tsl::ordered_map
    WIL
    redscript
)
