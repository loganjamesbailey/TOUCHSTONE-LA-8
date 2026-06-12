cmake_minimum_required(VERSION 3.22)

project(Touchstone VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
# v1 ships arm64; universal later is: -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13   # commit 7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(plugin)
add_subdirectory(tests)
