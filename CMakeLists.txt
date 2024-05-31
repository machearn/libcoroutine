cmake_minimum_required(VERSION 3.14)
project(libcoro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_DEBUG "Enable debug" ON)
option(BUILD_TESTS "Build tests" ON)

if(USE_DEBUG)
  set(CMAKE_BUILD_TYPE Debug)
  add_compile_definitions(DEBUG)
else()
  set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(include)
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(libcoro STATIC ${SOURCES})

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
