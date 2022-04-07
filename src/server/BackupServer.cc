#include "BackupServer.hh"

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
using grpc::StatusCode;
using std::cout;
using std::endl;
using std::string;

void BackupServer::SafeSetState(ReplState value) {
    std::unique_lock lock(stateMutex);
    repl_state = value;
}

Status BackupServer::FinishSync(ServerContext *context, const FinishSyncRequest *req, Ack *res) {
    auto status = PairedServer::FinishSync(context, req, res);
    if (status.ok()) {
        // Start new heartbeat thread if finished syncing
        std::thread([this] { heartbeat->Start(this); }).detach();
    }
    return status;
}

Status BackupServer::Heartbeat(ServerContext *context, const HeartbeatMessage *req, HeartbeatMessage *res) {
    // Backup should never receive these
    return Status(StatusCode::FAILED_PRECONDITION, "invalid target");
}

Status BackupServer::Read(ServerContext *context, const ReadRequest *req, ReadResponse *res) {
    if (SafeGetState() != ReplState::Standalone) {
        // Redirect client to the primary unless we're standalone
        return Status(StatusCode::ABORTED, "switch nodes");
    }

    // We are standalone, so process the req locally
    char buffer[BLOCK_SIZE];
    storage->read_data(req->address(), buffer);
    res->set_data(string(buffer, BLOCK_SIZE));
    return Status::OK;
}

Status BackupServer::Write(ServerContext *context, const WriteRequest *req, WriteResponse *res) {
    // Take a shared lock for the duration of this method.
    // This prevents a transition from Standalone to Normal from happening before we finish writing the data.
    std::shared_lock lock(stateMutex);
    if (repl_state != ReplState::Standalone) {
        // Redirect client to the primary unless we're Standalone
        return Status(StatusCode::ABORTED, "switch nodes");
    }

    // We are standalone, so process the req locally and mark it for use in recovery later
    auto data_str = req->data();

    // Sanity check
    if (data_str.length() != BLOCK_SIZE) {
        return Status(StatusCode::INVALID_ARGUMENT, "Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
    }

    auto address = req->address();
    auto data = data_str.c_str();

    storage->write_data(address, data);

#ifdef INCLUDE_CRASH_POINTS
    if (address == PREP_CRASH_ON_MESSAGE) {
        crash_flag = true;
    } else if (crash_flag) {
        if(address == CRASH_PRIMARY_BEFORE_BACKUP) {
            crash();
        } else if (address == PREP_CRASH_ON_NEXT_RECOVER) {
            make_crash_sentinel();
        }
    }
#endif

    replication->MarkDirty(address);

#ifdef INCLUDE_CRASH_POINTS
    if (crash_flag && address == CRASH_PRIMARY_AFTER_WRITE) {
        crash_after(1);
    }
#endif

    return Status::OK;
}

Status BackupServer::BackupWrite(ServerContext *context, const BackupWriteRequest *req, Ack *res) {
    auto address = req->address();
    
#ifdef INCLUDE_CRASH_POINTS
    if (crash_flag && address == CRASH_BACKUP_DURING_BACKUP) {
        crash();
    }
#endif

    switch (SafeGetState()) {
        case ReplState::Normal:
            // Commit data to disk
            storage->write_data(address, req->data().c_str());

#ifdef INCLUDE_CRASH_POINTS
            if (crash_flag && address == CRASH_BACKUP_DURING_BACKUP) {
                crash_after(1);
            }
            return Status::OK;
#endif
        case ReplState::Standalone:
            cout << "Assumption violated: Backup node received a replication message while acting as standalone." << endl;
            cout << "Both nodes are servicing client reqs. This suggests that a network partition has occurred." << endl;
            exit(1);

        case ReplState::Recovering:
            // Assume that we've restarted without the primary knowing about it;
            // therefore, fail this req, which will send the primary into Standalone if it wasn't already.

            return Status(StatusCode::UNAVAILABLE, "recovering");
        default:
            throw std::runtime_error("Invalid enum value");
    }
}

void BackupServer::HandlePartnerRecovered() {
    PairedServer::HandlePartnerRecovered();

    std::thread([this] { heartbeat->Start(this); }).detach();
}

BackupServer::BackupServer(ReplState initState, FileStorage *storage, ReplicationModule *replication, HeartbeatHelper *heartbeat) : PairedServer(initState, storage, replication), heartbeat(heartbeat) {}
