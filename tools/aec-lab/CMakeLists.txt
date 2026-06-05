# Standalone echo-cancellation measurement tool — see aec_lab.cpp.
# Deliberately not part of the client build: no Qt, runs anywhere GStreamer
# is installed (CI sweeps it on a Windows runner with the exact GStreamer
# version the family installers bundle).
cmake_minimum_required(VERSION 3.16)
project(aec-lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)

add_executable(aec-lab aec_lab.cpp)
target_link_libraries(aec-lab PkgConfig::GST)
