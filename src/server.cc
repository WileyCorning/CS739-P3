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
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>

#include "cmake/build/blockstorage.grpc.pb.h"

namespace fs = std::filesystem;
using blockstorageproto::BlockStorage;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::WriteRequest;
using blockstorageproto::WriteResponse;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using std::cout;
using std::endl;
using std::string;

#define BLOCK_SIZE 4096

class DiskModule {
    fs::path root;
    fs::path temp;

    fs::path get_main_path(string address) {
        return get_path_safely(root, address);
    }

    fs::path get_temp_path(string address) {
        // todo support concurrent write to different tempfiles
        return get_path_safely(temp, address);
    }

    fs::path get_path_safely(fs::path parent, string address) {
        auto normalized = (parent / address).lexically_normal();

        // Check that this path is under our storage root
        auto [a, b] = std::mismatch(parent.begin(), parent.end(), normalized.begin());
        if (a != parent.end()) {
            throw std::runtime_error("Normalized path is outside storage root");
        }

        return normalized;
    }

   public:
    DiskModule(fs::path root, fs::path temp) : root(root), temp(temp) {}

    void write_data(uint64_t address, string data) {
        // TODO replace with pwrite in a single file
        auto stable = get_main_path(std::to_string(address));
        auto transient = get_temp_path(std::to_string(address));

        std::ofstream file;
        file.open(transient, std::ios::binary);  // todo handle file.fail()
        file << data;
        file.close();

        rename(transient.c_str(), stable.c_str());  // todo handle err
    }

    string read_data(uint64_t address) {
        // TODO replace with pread in a single file
        auto stable = get_main_path(std::to_string(address));

        cout << "Addr " << address <<" to " << stable <<endl;
  
        char* buffer = new char[BLOCK_SIZE] {};
        
        if (fs::exists(stable)) {
            std::ifstream file(stable, std::ios::in | std::ios::binary);
            file.read(buffer, BLOCK_SIZE);  // todo handle file.fail()
            if(file.fail()) {
              cout << "Error reading file at " << stable.string() << endl;
            }
        } else {
          cout << "No file at " << stable.string() << endl;
        }
        
        cout << "Read  " << std::hex;
        for(int i = 0; i < 32; i++) {
          cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int) buffer[i]) << " ";
        }
        cout << std::dec << endl;


        return std::string(buffer, BLOCK_SIZE);
    }
};

// Logic and data behind the server's behavior.
class BlockStorageServiceImpl final : public BlockStorage::Service {
    DiskModule disk;

    Status Ping(ServerContext* context, const PingMessage* request, PingMessage* reply) override {
        return Status::OK;
    }

    Status Read(ServerContext* context, const ReadRequest* request, ReadResponse* reply) override {
        reply->set_data(disk.read_data(request->address()));
        return Status::OK;
    }

    Status Write(ServerContext* context, const WriteRequest* request, WriteResponse* reply) override {
        auto data_str = request->data();
        if (data_str.length() != BLOCK_SIZE) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
        }

        disk.write_data(request->address(), request->data());

        return Status::OK;
    }

   public:
    BlockStorageServiceImpl(DiskModule disk) : disk(disk) {}
};

void RunServer(DiskModule disk) {
    std::string server_address("0.0.0.0:50051");
    BlockStorageServiceImpl service(disk);

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

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

int main(int argc, char** argv) {
    if (argc != 3) {
        cout << "Usage: " << argv[0] << " root_folder temp_folder" << endl;
        return 1;
    }

    auto root = fs::canonical(argv[1]);
    auto temp = fs::canonical(argv[2]);

    cout << "Serving files from " << root << "; temp is " << temp << endl;

    RunServer(DiskModule(root, temp));
    return 0;
}
