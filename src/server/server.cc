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
#include "BackupReplication.hh"
#include "FileStorage.hh"
#include "NoReplication.hh"
#include "PrimaryReplication.hh"
#include "ReplicationModule.hh"
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

#define BLOCK_SIZE 4096
#define WHOLE_FILE "whole_file"

// Logic and data behind the server's behavior.
class BlockStorageServiceImpl final : public BlockStorage::Service {
    FileStorage *storage;
    ReplicationModule *replication;

    Status Ping(ServerContext *context, const PingMessage *request, PingMessage *reply) override {
        return Status::OK;
    }

    Status Read(ServerContext *context, const ReadRequest *request, ReadResponse *reply) override {
        char buffer[BLOCK_SIZE];
        storage->read_data(request->address(), buffer);
        reply->set_data(string(buffer, BLOCK_SIZE));
        return Status::OK;
    }

    Status Write(ServerContext *context, const WriteRequest *request, WriteResponse *reply) override {
        auto data_str = request->data();
        if (data_str.length() != BLOCK_SIZE) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
        }

        storage->write_data(request->address(), request->data().c_str());

        return Status::OK;
    }

   public:
    BlockStorageServiceImpl(FileStorage *storage, ReplicationModule *replication) : storage(storage), replication(replication) {}
};

void RunServer(string port, FileStorage *storage, ReplicationModule *replication) {
    string server_address = "0.0.0.0:" + port;  // "0.0.0.0:50051"
    BlockStorageServiceImpl service(storage, replication);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    
    replication->Initialize();

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();

    replication->TearDown();
}

string argErrString(string name) {
    return "Usage: " + name + " <port> ( standalone | primary --backup-address <backup-address> | backup --primary-address <primary-address> ) <storage_file>";
}

ReplicationModule *GetReplicationModule(string name, char **argv, size_t idx) {
    auto value = string(argv[idx]);
    if (value == "standalone")
        return new NoReplication();

    auto flag = string(argv[idx + 1]);
    auto target = string(argv[idx + 2]);

    if (value == "primary") {
        if (flag != "--backup-address") {
            throw std::runtime_error(argErrString(name));
        }
        return new PrimaryReplication(target);
    }

    if (value == "backup") {
        if (flag != "--primary-address") {
            throw std::runtime_error(argErrString(name));
        }
        return new BackupReplication(target);
    }

    throw std::runtime_error(argErrString(name));
}

int main(int argc, char **argv) {
    string name = argv[0];

    if (argc != 4 && argc != 6) {
        cout << argErrString(name) << endl;
        return 1;
    }

    string port = argv[1];

    auto is_standalone = string(argv[2]) == "standalone";

    auto fname_storage = fs::weakly_canonical(string(argv[is_standalone ? 3 : 5]));

    cout << "Using storage file " << fname_storage << endl;

    auto storage = FileStorage(fname_storage);
    storage.init(STORAGE_FILE_SIZE_MB);
    auto replication = GetReplicationModule(name, argv, 2);

    RunServer(port, &storage, replication);
    return 0;
}
