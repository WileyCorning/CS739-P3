#ifndef HEARTBEATHELPER_HH
#define HEARTBEATHELPER_HH

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <stdlib.h>
#include <time.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>

#include "../cmake/build/blockstorage.grpc.pb.h"
#include "../shared/CommonDefinitions.hh"
#include "FileStorage.hh"
#include "PairedServer.hh"
#include "ReplicationModule.hh"

namespace fs = std::filesystem;
using blockstorageproto::BlockStorage;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::WriteRequest;
using blockstorageproto::WriteResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using std::cout;
using std::endl;
using std::string;

#define HEARTBEAT_INTERVAL_MS 1000

class BackupServer;

class HeartbeatHelper {
    int iter = 0;
    std::mutex mutex;
    std::unique_ptr<BlockStorage::Stub> stub_;

   public:
    HeartbeatHelper(std::shared_ptr<Channel> channel);
    bool HeartbeatOnce();
    void Start(BackupServer* server);
};

#endif