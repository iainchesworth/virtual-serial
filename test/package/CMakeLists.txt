cmake_minimum_required(VERSION 3.16)
project(virtual_serial_consumer LANGUAGES CXX)

# Consumes the installed package exactly as an application would:
#   cmake -DCMAKE_PREFIX_PATH=<prefix> -DBOOST_ROOT=<boost> ...
find_package(boost_virtual_serial CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Boost::virtual_serial)
