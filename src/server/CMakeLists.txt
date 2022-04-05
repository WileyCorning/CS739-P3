
add_executable(server server.cc
        PrimaryReplication.cc
        PrimaryReplication.cc
        BackupReplication.cc
        FileStorage.cc
)
target_link_libraries(
        server
        hw_grpc_proto
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF}
)
