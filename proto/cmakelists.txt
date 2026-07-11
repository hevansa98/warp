add_library(proto_gen STATIC)

target_compile_features(proto_gen PUBLIC cxx_std_17)

target_link_libraries(proto_gen PUBLIC protobuf::libprotobuf)

target_include_directories(proto_gen PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

protobuf_generate(
    TARGET     proto_gen
    PROTOS     message.proto
    LANGUAGE   cpp
)
