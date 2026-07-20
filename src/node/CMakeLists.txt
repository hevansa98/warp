add_library(node SHARED
    node.cpp
)

target_include_directories(node PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_features(node PRIVATE cxx_std_20)

target_compile_options(node PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

find_package(Threads REQUIRED)
target_link_libraries(node PRIVATE Threads::Threads comm yaml-cpp::yaml-cpp PUBLIC proto_gen)