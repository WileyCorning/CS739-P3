
#include "PrimaryReplication.hh"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>

#include "cmake/build/blockstorage.grpc.pb.h"

namespace fs = std::filesystem;
using blockstorageproto::BlockStorage;
using blockstorageproto::HeartbeatMessage;
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

void PrimaryReplication::HeartbeatLoop() {
    while (!isDone) {
        if (HeartbeatOnce()) {
            AssumeBackupAvailable();
        } else {
            AssumeBackupOffline();
        }
        sleep(2);
    }
}

PrimaryReplication::PrimaryReplication(string backupAddr) {
    cout << "Initializing replication as Primary; Backup at " << backupAddr << endl;
    stub_ = BlockStorage::NewStub(grpc::CreateChannel(backupAddr, grpc::InsecureChannelCredentials()));
}

PrimaryReplication::~PrimaryReplication() {}

void PrimaryReplication::PingOnce() {
    PingMessage request;
    PingMessage reply;
    Status status;
    uint32_t retryCount = 0;

    // Retry w backoff
    do {
        ClientContext context;
        cout << "Attempting to ping the backup..." << endl;
        status = stub_->Ping(&context, request, &reply);
        if (status.ok()) {
            break;
        } else {
            sleep(1);
        }
    } while (!status.ok());
    cout << "Ping response received!" << endl;
}

bool PrimaryReplication::ShouldWriteLocally() {
    return true;  // TODO wait to write locally if currently reintegrating
}
void PrimaryReplication::AssumeBackupAvailable() {
    cout << "Backup appears available" << endl;
}

void PrimaryReplication::AssumeBackupOffline() {
    cout << "Backup appears offline" << endl;
}

bool PrimaryReplication::HeartbeatOnce() {
    HeartbeatMessage request;
    HeartbeatMessage reply;
    Status status;
    ClientContext context;
    cout << "Sending heartbeat to backup..." << endl;
    status = stub_->Heartbeat(&context, request, &reply);
    return status.ok();
}

void PrimaryReplication::Initialize() {
    if(didStart) {
        throw new std::runtime_error("Replication module cannot initialize twice");
    }
    didStart = true;
    heartbeatThread = std::thread([this] { HeartbeatLoop(); });
}

void PrimaryReplication::TearDown() {
    isDone = true;
    heartbeatThread.join();
}

void PrimaryReplication::HandleWriteByClient(uint64_t addr, char *data, size_t length) {
    // TODO send to backup
}