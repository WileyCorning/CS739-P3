
add_executable(server server.cc
        BackupServer.cc
        FileStorage.cc
        HeartbeatHelper.cc
        PairedServer.cc
        PrimaryServer.cc
        ReplicationModule.cc
)
target_link_libraries(
        server
        hw_grpc_proto
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF}
)
