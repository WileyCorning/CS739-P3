#ifndef PAIREDSERVER_HH
#define PAIREDSERVER_HH

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
#include "ReplicationModule.hh"
#include "Crash.hh"

namespace fs = std::filesystem;
using blockstorageproto::Ack;
using blockstorageproto::BackupWriteRequest;
using blockstorageproto::BlockStorage;
using blockstorageproto::FinishSyncRequest;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::SyncBlockRequest;
using blockstorageproto::TriggerSyncRequest;
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
using std::chrono::steady_clock;
using std::chrono::time_point;

#define RECOVERY_TIMEOUT_MS 10000
#define RECOVERY_CHECK_INTERVAL_MS 100

enum ReplState {
    Normal,
    Standalone,
    SendingSync,
    Recovering,
};

class RecoveryState {
   public:
    int sync_id = 0;
    size_t blocks_received = 0;
    time_point<steady_clock> last_progress;
    bool done = false;
    
    RecoveryState() {
        last_progress = steady_clock::now();
    }

    RecoveryState(int sync_id) : sync_id(sync_id) {
        last_progress = steady_clock::now();
    }
};

class PairedServer : public BlockStorage::Service {
   protected:
    virtual void BeginSynchronization(int partner_sync_id);
    virtual Status Ping(ServerContext *context, const PingMessage *req, PingMessage *res) override;
    virtual Status TriggerSync(ServerContext *context, const TriggerSyncRequest *req, Ack *res) override;
    virtual Status SyncBlock(ServerContext *context, const SyncBlockRequest *req, Ack *res) override;
    virtual Status FinishSync(ServerContext *context, const FinishSyncRequest *req, Ack *res) override;

    FileStorage *storage;
    ReplicationModule *replication;
    
    RecoveryState recovery;
    ReplState repl_state;
    
    std::shared_mutex stateMutex;
    std::shared_mutex recoveryMutex;
    
    virtual void HandlePartnerRecovered();
    

   public:
    PairedServer(ReplState initState, FileStorage *storage, ReplicationModule *replication);
    ReplState SafeGetState();
    virtual ~PairedServer() {}
    virtual void WaitForCounterpart();
    virtual void Recover();
};

#endif