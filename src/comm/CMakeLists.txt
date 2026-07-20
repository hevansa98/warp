add_library(comm SHARED
    tcp_handler.cpp
    udp_handler.cpp
    comm_handler.cpp
)

target_include_directories(comm PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_compile_features(comm PRIVATE cxx_std_20)

target_compile_options(comm PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

find_package(Threads REQUIRED)
target_link_libraries(comm PRIVATE Threads::Threads)