#include "PrimaryServer.hh"

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
#include <exception>
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
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::SyncBlockRequest;
using blockstorageproto::TriggerSyncRequest;
using blockstorageproto::WriteRequest;
using blockstorageproto::WriteResponse;
using blockstorageproto::HeartbeatMessage;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::StatusCode;
using grpc::Status;
using std::cout;
using std::endl;
using std::string;
using std::chrono::steady_clock;
using std::chrono::time_point;

Status PrimaryServer::Heartbeat(ServerContext *context, const HeartbeatMessage *req, HeartbeatMessage *res) {
    switch (SafeGetState()) {
        case ReplState::Normal:
            return Status::OK;
        case ReplState::Standalone:
            cout << "Assumption violated: Primary node received a heartbeat message while acting as standalone." << endl;
            cout << "The primary has inferred a backup failure without the backup restarting. This suggests that a network partition has occurred." << endl;
            exit(1);
        case ReplState::Recovering:
            // The primary has crashed and restarted without the backup finding out about it yet.
            // We choose to  fail this req, sending the backup into standalone mode.
            return Status(StatusCode::ABORTED, "recovery in progress");
        default:
            throw std::runtime_error("Invalid enum value");
    }
}

Status PrimaryServer::Read(ServerContext *context, const ReadRequest *req, ReadResponse *res) {
    if (SafeGetState() == ReplState::Recovering) {
        // Redirect client to the backup while we're recovering
        return Status(StatusCode::ABORTED, "switch nodes");
    }

    // If we're functioning normally or standalone, perform the read
    char buffer[BLOCK_SIZE];
    storage->read_data(req->address(), buffer);
    res->set_data(string(buffer, BLOCK_SIZE));
    return Status::OK;
}

Status PrimaryServer::Write(ServerContext *context, const WriteRequest *req, WriteResponse *res) {
    if (SafeGetState() == ReplState::Recovering) {
        // Redirect client to the backup while we're recovering
        return Status(StatusCode::ABORTED, "switch nodes");
    }

    auto data_str = req->data();

    // Sanity check
    if (data_str.length() != BLOCK_SIZE) {
        return Status(StatusCode::INVALID_ARGUMENT, "Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
    }

    auto address = req->address();
    auto data = data_str.c_str();

    storage->write_data(address, data);
    BackupIfPossible(address, data);

    return Status::OK;
}

void PrimaryServer::BackupIfPossible(uint64_t address, const char *data) {
    std::shared_lock lock(stateMutex);
    switch (repl_state) {
        case ReplState::Normal:
            lock.unlock();  // Release the read lock before making an RPC call
            if (!replication->TrySendBackupWrite(address, data)) {
                // If we fail the req, assume the backup has crashed and go to Standalone
                cout << "Backup appears to be down; switching to Standalone" << endl;
                std::unique_lock lock0(stateMutex);
                repl_state = ReplState::Standalone;
                replication->MarkDirty(address);
            }
            return;
        case ReplState::Standalone:
            // Hold the read lock, in case a sync is in progress
            replication->MarkDirty(address);
            return;
        case ReplState::Recovering:
            throw std::runtime_error("Attempting to send backup while in recovery (should never happen)");
    }
}

Status PrimaryServer::BackupWrite(ServerContext *context, const BackupWriteRequest *req, Ack *res) {
    // Primary should never receive these
    return Status(StatusCode::FAILED_PRECONDITION,"invalid target");
}

PrimaryServer::PrimaryServer(ReplState initState, FileStorage *storage, ReplicationModule *replication) : PairedServer(initState, storage, replication) {}
