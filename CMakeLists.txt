cmake_minimum_required(VERSION 3.24)

project(thf VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

option(THIFO_BUILD_TESTS "Build the regression tests (ctest)" OFF)
option(THIFO_INSTALL_PLUGINS "Copy plug-ins to the user plug-in folders after build" ON)

include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

include(shared/cmake/ThifoPlugin.cmake)

add_subdirectory(plugins/granular)

if(THIFO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
