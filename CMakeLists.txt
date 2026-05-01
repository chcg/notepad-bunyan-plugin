cmake_minimum_required(VERSION 3.21)

project(BunyanLogViewer LANGUAGES CXX)

add_library(BunyanLogViewer SHARED
    src/plugin.cpp
)

target_compile_features(BunyanLogViewer PRIVATE cxx_std_20)
target_compile_definitions(BunyanLogViewer PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_include_directories(BunyanLogViewer PRIVATE src)

if(MSVC)
    target_compile_options(BunyanLogViewer PRIVATE /W4 /permissive-)
endif()

set_target_properties(BunyanLogViewer PROPERTIES
    OUTPUT_NAME "BunyanLogViewer"
)