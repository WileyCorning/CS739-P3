#include "HeartbeatHelper.hh"

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
#include "BackupServer.hh"
#include "ReplicationModule.hh"

namespace fs = std::filesystem;
using blockstorageproto::BlockStorage;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::WriteRequest;
using blockstorageproto::WriteResponse;
using blockstorageproto::HeartbeatMessage;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using std::cout;
using std::endl;
using std::string;



HeartbeatHelper::HeartbeatHelper(std::shared_ptr<Channel> channel) : stub_(BlockStorage::NewStub(channel)) {}

bool HeartbeatHelper::HeartbeatOnce() {
    HeartbeatMessage req;
    HeartbeatMessage res;
    Status status;
    ClientContext context;
    cout << "Sending heartbeat to primary" << endl;
    status = stub_->Heartbeat(&context, req, &res);
    return status.ok();
}

void HeartbeatHelper::Start(BackupServer* server) {
    mutex.lock();
    auto my_iter = ++iter;
    mutex.unlock();

    bool ok = HeartbeatOnce();
    while (iter == my_iter && ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
        ok = HeartbeatOnce();
    }

    if (ok) {
        cout << "Heartbeat loop replaced" << endl;
    } else {
        cout << "Primary appears to be down; switching to standalone" << endl;
        server->SafeSetState(ReplState::Standalone);
    }
}
