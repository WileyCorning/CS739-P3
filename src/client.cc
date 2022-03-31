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

#include <fcntl.h>
#include <grpcpp/grpcpp.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <iomanip>

#include "cmake/build/blockstorage.grpc.pb.h"

using blockstorageproto::BlockStorage;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::WriteRequest;
using blockstorageproto::WriteResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using std::cout;
using std::endl;

#define BLOCK_SIZE 4096
#define DEBUG 1

class BlockStorageClient {
    void print_status(std::string of, Status status) {
        if (status.ok()) {
            printf("%s: RPC Success\n", of.c_str());
        } else {
            printf("%s: RPC Failed with code %d:\n\t%s", of.c_str(), status.error_code(), status.error_message().c_str());
        }
    }

   public:
    BlockStorageClient(std::shared_ptr<Channel> channel)
        : stub_(BlockStorage::NewStub(channel)) {}

    int Ping(std::chrono::nanoseconds* round_trip_time) {
        auto start = std::chrono::steady_clock::now();

        PingMessage request;
        PingMessage reply;
        Status status;
        uint32_t retryCount = 0;

        // Retry w backoff
        ClientContext context;
        printf("Ping: Invoking RPC\n");
        status = stub_->Ping(&context, request, &reply);

        // Checking RPC Status
        if (status.ok()) {
            printf("Ping: RPC Success\n");
            auto end = std::chrono::steady_clock::now();
            *round_trip_time = end - start;
#if DEBUG
            std::chrono::duration<double, std::ratio<1, 1>> seconds = end - start;
            printf("Ping: Exiting function (took %fs)\n", seconds.count());
#endif
            return 0;
        } else {
            printf("Ping: RPC failure\n");
            printf("Ping: Exiting function\n");
            return -1;
        }
    }

    void Write(uint64_t address, char* buffer, size_t n) {
      printf("in Write\n");
        if (n != BLOCK_SIZE) {
            throw std::runtime_error("Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(n) + ")");
        }
        WriteRequest request;
        WriteResponse reply;
        Status status;
        ClientContext context;

        std::string data_str(buffer, n);

        request.set_address(address);
        request.set_data(data_str);

        printf("Read: Invoking RPC\n");
        status = stub_->Write(&context, request, &reply);

#if DEBUG
        print_status("Write", status);
#endif
        if(!status.ok()) {
          throw std::runtime_error("Status wasn't ok");
        }
    }

    void Read(uint64_t address, char* buffer, size_t n) {
      printf("in Read\n");
      
        if (n != BLOCK_SIZE) {
            throw std::runtime_error("Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(n) + ")");
        }

        ReadRequest request;
        ReadResponse reply;
        Status status;

        ClientContext context;
        
        request.set_address(address);
        
        printf("Read: Invoking RPC\n");

        status = stub_->Read(&context, request, &reply);

#if DEBUG
        print_status("Read", status);
#endif
        if(!status.ok()) {
          throw std::runtime_error("Status wasn't ok");
        }
        auto data_str = reply.data();

        if (data_str.length() != BLOCK_SIZE) {
            throw std::runtime_error("Received data block of wrong size: should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
        }
        data_str.copy(buffer,n);
        // memcpy(buffer, data_str.data(), n);
    }

   private:
    std::unique_ptr<BlockStorage::Stub> stub_;
};

int main(int argc, char** argv) {
    // Instantiate the client. It requires a channel, out of which the actual RPCs
    // are created. This channel models a connection to an endpoint specified by
    // the argument "--target=" which is the only expected argument.
    // We indicate that the channel isn't authenticated (use of
    // InsecureChannelCredentials()).
    std::string target_str;
    std::string arg_str("--target");
    if (argc > 1) {
        std::string arg_val = argv[1];
        size_t start_pos = arg_val.find(arg_str);
        if (start_pos != std::string::npos) {
            start_pos += arg_str.size();
            if (arg_val[start_pos] == '=') {
                target_str = arg_val.substr(start_pos + 1);
            } else {
                std::cout << "The only correct argument syntax is --target="
                          << std::endl;
                return 0;
            }
        } else {
            std::cout << "The only acceptable argument is --target=" << std::endl;
            return 0;
        }
    } else {
        target_str = "localhost:50051";
    }
    BlockStorageClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
      
    std::chrono::nanoseconds ping_time;
    client.Ping(&ping_time);

    std::string an_input_string("Example input string (hello world!)");

    char buffer_in[BLOCK_SIZE] = {};
    char buffer_out[BLOCK_SIZE] = {};

    std::memcpy(buffer_in, an_input_string.data(), an_input_string.length());

    uint64_t addr = 2022;
    client.Write(addr, buffer_in, BLOCK_SIZE);
    client.Read(addr, buffer_out, BLOCK_SIZE);

    std::string returned(buffer_out, an_input_string.length());
    
    std::cout << an_input_string << ":::" << returned << std::endl;
    
    int k = an_input_string.length();
    
    cout << "Sent " << std::hex;
    for(int i = 0; i < k; i++) {
      cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int) buffer_in[i]) << " ";
    }
    cout << std::dec << endl;
    
    cout << "Got  " << std::hex;
    for(int i = 0; i < k; i++) {
      cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int) buffer_out[i]) << " ";
    }
    cout << std::dec << endl;

    printf("Sent [%s], got back [%s]\n", an_input_string.c_str(), returned.c_str());

    return 0;
}
