/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
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

#include "../shared/CommonDefinitions.hh"
#include "PrimaryServer.hh"
#include "BackupServer.hh"
#include "PairedServer.hh"
#include "HeartbeatHelper.hh"
#include "FileStorage.hh"
#include "ReplicationModule.hh"
#include "../cmake/build/blockstorage.grpc.pb.h"

namespace fs = std::filesystem;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using std::cout;
using std::endl;
using std::string;


// // Logic and data behind the server's behavior.
// class BlockStorageServiceImpl final : public BlockStorage::Service {
//     FileStorage *storage;
//     ReplicationModule *replication;

//     Status Ping(ServerContext *context, const PingMessage *req, PingMessage *res) override {
//         return Status::OK;
//     }

//     Status Read(ServerContext *context, const ReadRequest *req, ReadResponse *res) override {
//         char buffer[BLOCK_SIZE];
//         storage->read_data(req->address(), buffer);
//         res->set_data(string(buffer, BLOCK_SIZE));
//         return Status::OK;
//     }

//     Status Write(ServerContext *context, const WriteRequest *req, WriteResponse *res) override {
//         auto data_str = req->data();
//         if (data_str.length() != BLOCK_SIZE) {
//             return Status(grpc::StatusCode::INVALID_ARGUMENT, "Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
//         }

//         storage->write_data(req->address(), req->data().c_str());

//         return Status::OK;
//     }

//    public:
//     BlockStorageServiceImpl(FileStorage *storage, ReplicationModule *replication) : storage(storage), replication(replication) {}
// };

void RunServer(PairedServer* service, string port) {
    string server_address = "0.0.0.0:" + port;  // "0.0.0.0:50051"

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(service);
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    
    if(service->SafeGetState() == ReplState::Recovering) {
        std::thread([service] { service->Recover(); }).detach();
    }
    
    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

string argErrString(string name) {
    return "Usage: " + name + " <port> ( primary --backup-address <backup-address> | backup --primary-address <primary-address> ) <storage_file> [--recover]";
}

PairedServer* MakeServer(bool recovering, string kind, FileStorage* storage, std::shared_ptr<grpc::Channel> partnerChannel) {
    auto replication = new ReplicationModule(partnerChannel);
    if(kind == "primary") {
        return new PrimaryServer(recovering ? ReplState::Recovering : ReplState::Standalone,storage,replication);
    }
    if (kind == "backup") {
        // Wait for the primary to come online before starting the backup
        replication->PingOnce();
        auto heartbeat = new HeartbeatHelper(partnerChannel);
        return new BackupServer(ReplState::Recovering,storage,replication,heartbeat);
    }
    throw std::runtime_error(argErrString(kind));
}

int main(int argc, char **argv) {
    string name = argv[0];

    if (argc != 6 && argc != 7) {
        cout << argErrString(name) << endl;
        return 1;
    }

    string port = argv[1];
    
    string kind = argv[2];
    string specifier = argv[3];
    
    if(
        (kind == "primary" && specifier != "--backup-address") ||
        (kind == "backup" && specifier != "--primary-address")
    ) {
        cout << argErrString(name) << endl;
        return 1;
    }
    
    string target = argv[4];
    
    auto is_standalone = string(argv[2]) == "standalone";

    
    bool is_recover = false;
    if(argc > 6) {
        if(string(argv[6]) != "--recover") {
            cout << argErrString(name) << endl;
            return 1;
        }
        is_recover = true;
    }
    
    auto fname_storage = fs::weakly_canonical(string(argv[5]));
    cout << "Using storage file " << fname_storage << endl;

    auto storage = FileStorage(fname_storage);
    storage.init(STORAGE_FILE_SIZE_MB);
    
    auto partnerChannel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    
    auto server = MakeServer(is_recover,kind,&storage,partnerChannel);

    RunServer(server,port);
    return 0;
}
