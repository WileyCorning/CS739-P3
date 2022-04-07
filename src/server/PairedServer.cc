#include "PairedServer.hh"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <stdlib.h>
#include <time.h>

#include <chrono>
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

PairedServer::PairedServer(ReplState initState, FileStorage *storage, ReplicationModule *replication) : repl_state(initState), storage(storage), replication(replication)  {}

Status PairedServer::Ping(ServerContext *context, const PingMessage *req, PingMessage *res) {
    return Status::OK;
}

Status PairedServer::TriggerSync(ServerContext *context, const TriggerSyncRequest *req, Ack *res) {
    auto sync_id = req->sync_id();
    
    std::unique_lock lock(stateMutex);
    switch (repl_state) {
        case ReplState::Normal:
        case ReplState::Standalone:
            repl_state = ReplState::Standalone;
            lock.unlock();

            // Begin resync process on separate thread
            std::thread([this, sync_id] { BeginSynchronization(sync_id); }).detach();

            // Respond to this req immediately to avoid gRPC timeout
            return Status::OK;
        case ReplState::Recovering:
            cout << "Assumption violated: primary and backup are both in recovery mode." << endl;
            cout << "This indicates that a server has crashed while the other was crashed or recovering." << endl;
            exit(1);
        default:
            throw std::runtime_error("Invalid enum value");
    }
}

void PairedServer::BeginSynchronization(int partner_sync_id) {
    cout << "Sending recovery information to other node" << endl;
    
    // Note: any overlapping sync attempts will terminate automatically
    // when their reqs are rejected by the counterpart.

    std::unique_lock lock(stateMutex, std::defer_lock);
    
    if (replication->TryPerformSync(partner_sync_id, &lock, storage)) {
        // On success, the lock will be returned in a held state
        repl_state = ReplState::Normal;
        replication->ClearDirty();
        lock.unlock();
        cout << "Finished recovery of other server" << endl;
        HandlePartnerRecovered();
    } else {
        cout << "Recovery of other server incomplete" << endl;
    }
}
void PairedServer::HandlePartnerRecovered() { }

ReplState PairedServer::SafeGetState() {
    std::shared_lock lock(stateMutex);
    return repl_state;
}

// Received while this node is recovering
Status PairedServer::SyncBlock(ServerContext *context, const SyncBlockRequest *req, Ack *res) {
    if (SafeGetState() != ReplState::Recovering) {
        // If received in another mode, this req must be stale
        return Status(StatusCode::CANCELLED, "stale sync");
    }

    std::unique_lock lock(recoveryMutex);
    if (req->sync_id() != recovery.sync_id) {
        // This req is probably from an earlier sync, so cancel it
        return Status(StatusCode::CANCELLED, "stale sync");
    }

    // Commit this block
    storage->write_data(req->address(), req->data().c_str());
    recovery.blocks_received += 1;
    recovery.last_progress = steady_clock::now();

    return Status::OK;
}

// Received while this node is recovering
Status PairedServer::FinishSync(ServerContext *context, const FinishSyncRequest *req, Ack *res) {
    if (SafeGetState() != ReplState::Recovering) {
        // This is not useful in other modes
        return Status(StatusCode::FAILED_PRECONDITION, "stale sync");
    }

    std::unique_lock lock(recoveryMutex);
    if (req->sync_id() != recovery.sync_id) {
        // This req is probably from an earlier sync, so cancel it
        return Status(StatusCode::CANCELLED, "stale sync");
    }

    if (req->total_blocks() != recovery.blocks_received) {
        // We haven't received all the blocks
        // This should only happen given a network partition
        cout << "Recovery failed: expected " << req->total_blocks() << " blocks, got " << recovery.blocks_received << endl;
        return Status(StatusCode::ABORTED, "incomplete sync");
    }
    
#ifdef INCLUDE_CRASH_POINTS
    if(has_crash_sentinel()) {
        clear_crash_sentinel();
        crash();
    }
#endif
    
    // Go to normal operation
    recovery.done = true;
    cout << "Finished recovery ( " << recovery.blocks_received << " blocks received)" << endl;
    std::unique_lock lock_state(stateMutex);
    repl_state = ReplState::Normal;
    return Status::OK;
}

void PairedServer::Recover() {
    int sync_id;
    srand(time(NULL));  // Seed RNG with time

    do {
        std::unique_lock lock(recoveryMutex);
        sync_id = rand();  // Randomize sync id
        recovery = RecoveryState(sync_id);
        lock.unlock();

        cout << "Starting recovery attempt (sync_id " << sync_id << ")" << endl;
        if (!replication->TrySendTriggerSync(sync_id)) {
            cout << "Assumption violated: unable to start recovery process." << endl;
            cout << "This indicates that the other server is not available as this one is restarting." << endl;
            exit(1);
        }

        // Wait until we've finished successfully or we have exceeded the timeout
        while (!recovery.done && std::chrono::steady_clock::now() - recovery.last_progress < std::chrono::milliseconds(RECOVERY_TIMEOUT_MS)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(RECOVERY_CHECK_INTERVAL_MS));
        }
    } while (!recovery.done);
    
    cout << "Confirm recovery" << endl;
}

void PairedServer::WaitForCounterpart() {
}
