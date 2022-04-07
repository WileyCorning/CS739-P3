#include "ReplicationModule.hh"

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
using grpc::StatusCode;
using std::cout;
using std::endl;
using std::string;
using std::chrono::steady_clock;
using std::chrono::time_point;

ReplicationModule::ReplicationModule(std::shared_ptr<Channel> channel) : stub_(BlockStorage::NewStub(channel)) {}

void ReplicationModule::PingOnce() {
    PingMessage req;
    PingMessage res;
    Status status;
    uint32_t retryCount = 0;

    // Retry w backoff
    do {
        ClientContext context;
        cout << "Attempting to ping the other server..." << endl;
        status = stub_->Ping(&context, req, &res);
        if (status.ok()) {
            break;
        } else {
            sleep(1);
        }
    } while (!status.ok());
    cout << "Ping response received!" << endl;
}

void ReplicationModule::MarkDirty(uint64_t address) {
    auto r = dirtySet.emplace(address);
    if (r.second) {
        dirtyVec.emplace_back(address);
    }
}

void ReplicationModule::ClearDirty() {
    dirtySet.clear();
    dirtyVec.clear();
}

bool ReplicationModule::TrySendBackupWrite(uint64_t address, const char* buffer) {
    BackupWriteRequest req;
    Ack res;
    Status status;
    ClientContext context;

    std::string data_str(buffer, BLOCK_SIZE);

    req.set_address(address);
    req.set_data(data_str);

    status = stub_->BackupWrite(&context, req, &res);
    return status.ok();
}

bool ReplicationModule::TrySendTriggerSync(int sync_id) {
    TriggerSyncRequest req;
    Ack res;
    Status status;
    ClientContext context;
    req.set_sync_id(sync_id);
    status = stub_->TriggerSync(&context, req, &res);
    return status.ok();
}

bool ReplicationModule::TrySendSyncBlock(int sync_id, uint64_t address, char* buffer) {
    SyncBlockRequest req;
    Ack res;
    Status status;
    ClientContext context;
    req.set_sync_id(sync_id);
    req.set_address(address);
    req.set_data(string(buffer, BLOCK_SIZE));
    status = stub_->SyncBlock(&context, req, &res);
    return status.ok();
}

bool ReplicationModule::TrySendFinishSync(int sync_id, size_t block_count) {
    FinishSyncRequest req;
    Ack res;
    Status status;
    ClientContext context;
    req.set_sync_id(sync_id);
    req.set_total_blocks(block_count);

    status = stub_->FinishSync(&context, req, &res);
    return status.ok();
}

// Lock *must* be passed in an unlocked state.
// It will return locked on success, or else in any state.
bool ReplicationModule::TryPerformSync(int sync_id, std::unique_lock<std::shared_mutex>* lock, FileStorage* storage) {
    size_t i = 0;
    uint64_t address;

    while (true) {
        lock->lock();
        if (i < dirtyVec.size()) {
            address = dirtyVec[i++];
            
            // If we have more entries yet to process, unlock to allow more to be added
            lock->unlock();
        } else {
            // If we have reached the end of the queue, break while still holding the lock
            break;
        }

        char buffer[BLOCK_SIZE];
        storage->read_data(address, buffer);

        if (!TrySendSyncBlock(sync_id, dirtyVec[i], buffer)) {
            cout << "Failed to sync block to recovering partner" << endl;
            // Return without lock held
            return false;
        }
    }

    // Return with lock still held
    auto ok = TrySendFinishSync(sync_id, i);
    if(!ok) {
        cout << "FinishSync unsuccessful" << endl;
    }
    return ok;
}