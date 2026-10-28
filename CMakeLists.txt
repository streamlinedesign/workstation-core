cmake_minimum_required(VERSION 3.22)
project(workstation-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(WSC_BUILD_TESTS "Build unit tests" ON)
option(WSC_WERROR "Treat warnings as errors" OFF)

find_package(ALSA REQUIRED)
find_package(Threads REQUIRED)

add_library(wsc
  src/alsa_device.cpp
  src/rt.cpp
  src/midi_input.cpp
  src/midi_parser.cpp
  src/sine_engine.cpp
  src/core.cpp
)
target_include_directories(wsc PUBLIC include)
target_link_libraries(wsc PUBLIC ALSA::ALSA Threads::Threads)
target_compile_options(wsc PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
  $<$<BOOL:${WSC_WERROR}>:-Werror>)
# The audio thread must never trap on denormals; flush-to-zero is set at runtime too.
target_compile_options(wsc PUBLIC -fno-math-errno -fno-trapping-math)

add_executable(wsc-play apps/wsc_play.cpp)
target_link_libraries(wsc-play PRIVATE wsc)

add_executable(wsc-devices apps/wsc_devices.cpp)
target_link_libraries(wsc-devices PRIVATE wsc)

install(TARGETS wsc-play wsc-devices RUNTIME DESTINATION bin)

if(WSC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
