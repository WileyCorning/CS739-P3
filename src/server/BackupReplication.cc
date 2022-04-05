#include "BackupReplication.hh"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <shared_mutex>
#include <thread>

#include "../cmake/build/blockstorage.grpc.pb.h"

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

BackupReplication::BackupReplication(string primaryAddr)
{
    cout << "Initializing replication as Backup; Primary at " << primaryAddr << endl;
    stub_ = BlockStorage::NewStub(grpc::CreateChannel(primaryAddr, grpc::InsecureChannelCredentials()));
}

BackupReplication::~BackupReplication() {}

void BackupReplication::PingOnce()
{
    PingMessage request;
    PingMessage reply;
    Status status;
    uint32_t retryCount = 0;

    // Retry w backoff
    do
    {
        ClientContext context;
        cout << "Attempting to ping the primary..." << endl;
        status = stub_->Ping(&context, request, &reply);
        if (status.ok())
        {
            break;
        }
        else
        {
            sleep(1);
        }
    } while (!status.ok());
    cout << "Ping response received!" << endl;
}

bool BackupReplication::ShouldWriteLocally()
{
    return false; // TODO write locally if primary is offline
}

void BackupReplication::HandleWriteByClient(uint64_t addr, char *data, size_t length)
{
    // TODO forward to primary if it is online
}