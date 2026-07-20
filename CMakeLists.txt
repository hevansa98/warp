cmake_minimum_required(VERSION 3.31.6)

project(dist-comms)

find_package(yaml-cpp  REQUIRED)
find_package(absl      CONFIG REQUIRED)
find_package(Protobuf  REQUIRED)

add_subdirectory(proto)
add_subdirectory(src)
