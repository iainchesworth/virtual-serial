cmake_minimum_required (VERSION 3.19 FATAL_ERROR)

set(VCPKG_ROOT "vcpkg")
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

project (asio_serial VERSION 0.0.1 LANGUAGES CXX)

find_package(boost-asio REQUIRED)

add_executable (asio_serial src/asio_serial.cpp src/asio_serial.h)
target_compile_features(asio_serial PRIVATE cxx_std_20)

target_link_libraries(asio_serial
  PRIVATE
    boost::asio)