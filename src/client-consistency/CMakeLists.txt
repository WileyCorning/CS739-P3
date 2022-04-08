
add_executable(client-consistency
        client.cc
)
target_link_libraries(
        client-consistency
        hw_grpc_proto
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF}
)
