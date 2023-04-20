cmake_minimum_required(VERSION 3.8)
project(mcaptool LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(argparse REQUIRED)
find_package(ffmpeg REQUIRED)
find_package(fmt REQUIRED)
find_package(mcap REQUIRED)
find_package(Protobuf 3 REQUIRED)
find_package(spdlog REQUIRED)

message("Building with CMake version: ${CMAKE_VERSION}")

# Generate protobuf files
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/foxglove/CompressedImage.proto)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(OPTIMIZATION_FLAGS "-DDEBUG")
  if(NOT MSVC)
    set(OPTIMIZATION_FLAGS "${OPTIMIZATION_FLAGS} -O0")
  endif()
  message("-- Configuring debug build")
else()
  set(OPTIMIZATION_FLAGS "-DNDEBUG -O3")
  message("-- Configuring release build")
endif()

if(MSVC)
  set(DESIRED_WARNINGS "-WX")
else()
  set(DESIRED_WARNINGS "-Wall -Wextra -Wconversion -Wunreachable-code -Wuninitialized -pedantic-errors -Wold-style-cast -Wno-error=unused-variable -Wshadow -Wfloat-equal")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(DESIRED_WARNINGS "${DESIRED_WARNINGS} -Wmost")
  endif()
  if (WERROR)
    set(DESIRED_WARNINGS "${DESIRED_WARNINGS} -Werror")
  endif()
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OPTIMIZATION_FLAGS} ${DESIRED_WARNINGS}")

include_directories("${PROJECT_SOURCE_DIR}/include")

add_executable(mcaptool
  ${PROTO_SRCS}
  ${PROTO_HDRS}
  src/convert.cpp
  src/mcaptool.cpp
  src/protobuf.cpp
  src/split.cpp
  src/video.cpp
)
target_link_libraries(mcaptool
  argparse::argparse
  ffmpeg::avcodec
  ffmpeg::avformat
  ffmpeg::avutil
  fmt::fmt
  mcap::mcap
  protobuf::libprotobuf
  spdlog::spdlog
)
target_include_directories(mcaptool SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}) # for protobuf generated headers

# file(GLOB TEST_SOURCES test/*.cpp)
# add_executable(unit-tests ${TEST_SOURCES})
# target_link_libraries(unit-tests CONAN_PKG::catch2)
