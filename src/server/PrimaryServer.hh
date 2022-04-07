#ifndef PRIMARYSERVER_HH
#define PRIMARYSERVER_HH

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
#include "Crash.hh"

namespace fs = std::filesystem;
using blockstorageproto::BlockStorage;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::HeartbeatMessage;
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

class PrimaryServer : public PairedServer {
    bool crash_flag = false;
    
    virtual Status Heartbeat(ServerContext *context, const HeartbeatMessage *req, HeartbeatMessage *res) override;
    virtual Status Read(ServerContext *context, const ReadRequest *req, ReadResponse *res) override;
    virtual Status Write(ServerContext *context, const WriteRequest *req, WriteResponse *res) override;
    virtual Status BackupWrite(ServerContext *context, const BackupWriteRequest *req, Ack *res) override;

    void BackupIfPossible(uint64_t address, const char *data);
    
    public:
        PrimaryServer(ReplState initState, FileStorage *storage, ReplicationModule *replication);
    
};

#endif