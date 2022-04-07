#ifndef BACKUPSERVER_HH
#define BACKUPSERVER_HH

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
#include "HeartbeatHelper.hh"
#include "PairedServer.hh"
#include "ReplicationModule.hh"

namespace fs = std::filesystem;
using blockstorageproto::Ack;
using blockstorageproto::BackupWriteRequest;
using blockstorageproto::BlockStorage;
using blockstorageproto::FinishSyncRequest;
using blockstorageproto::HeartbeatMessage;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::SyncBlockRequest;
using blockstorageproto::TriggerSyncRequest;
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

class BackupServer : public PairedServer {
    HeartbeatHelper* heartbeat;

    virtual Status FinishSync(ServerContext *context, const FinishSyncRequest *req, Ack *res) override;
    virtual Status Heartbeat(ServerContext *context, const HeartbeatMessage *req, HeartbeatMessage *res) override;
    virtual Status Read(ServerContext *context, const ReadRequest *req, ReadResponse *res) override;
    virtual Status Write(ServerContext *context, const WriteRequest *req, WriteResponse *res) override;
    virtual Status BackupWrite(ServerContext *context, const BackupWriteRequest *req, Ack *res) override;
    
    virtual void HandlePartnerRecovered() override;
    
   public:
    BackupServer(ReplState initState, FileStorage *storage, ReplicationModule *replication, HeartbeatHelper* heartbeat);
    void SafeSetState(ReplState value);
};
#endif