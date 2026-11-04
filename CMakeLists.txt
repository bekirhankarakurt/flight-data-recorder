cmake_minimum_required(VERSION 3.16)
project(flight_data_recorder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FDR_BUILD_TESTS "Build the unit tests" ON)
option(FDR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(FDR_SANITIZE "Enable AddressSanitizer and UBSan" OFF)

add_library(fdr_options INTERFACE)
target_compile_options(fdr_options INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
if(FDR_WARNINGS_AS_ERRORS)
  target_compile_options(fdr_options INTERFACE -Werror)
endif()
if(FDR_SANITIZE)
  target_compile_options(fdr_options INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer)
  target_link_options(fdr_options INTERFACE -fsanitize=address,undefined)
endif()

file(GLOB FDR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(fdr_core STATIC ${FDR_SOURCES})
target_include_directories(fdr_core PUBLIC include)
target_link_libraries(fdr_core PUBLIC fdr_options)

if(FDR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
