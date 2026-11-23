cmake_minimum_required(VERSION 3.30)
project(kcd1cursor VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "kcd1cursor requires Windows x64 and MSVC")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(FetchContent)
FetchContent_Declare(MinHook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(MinHook)

add_library(kcd1cursor SHARED src/Plugin.cpp)
target_compile_features(kcd1cursor PRIVATE cxx_std_20)
target_compile_options(kcd1cursor PRIVATE /W4 /utf-8)
target_compile_definitions(kcd1cursor PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(kcd1cursor PRIVATE minhook)
set_target_properties(kcd1cursor PROPERTIES SUFFIX ".asi")
