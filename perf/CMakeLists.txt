# The performance suite is built explicitly with
# -DBOOST_VIRTUAL_SERIAL_BUILD_PERF=ON; it is not part of the default build.

find_package(Threads REQUIRED)

add_executable(boost_virtual_serial_perf perf_main.cpp)
set_target_properties(boost_virtual_serial_perf PROPERTIES
  OUTPUT_NAME virtual_serial_perf
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)

target_link_libraries(boost_virtual_serial_perf
  PRIVATE Boost::virtual_serial Threads::Threads)

# The benchmarks must never depend on MSVC auto-linking Boost libraries.
target_compile_definitions(boost_virtual_serial_perf PRIVATE BOOST_ALL_NO_LIB)

if(COMMAND boost_virtual_serial_set_warnings)
  boost_virtual_serial_set_warnings(boost_virtual_serial_perf)
endif()
