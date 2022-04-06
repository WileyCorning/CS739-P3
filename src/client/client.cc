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

#include "../cmake/build/blockstorage.grpc.pb.h"

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
#define INSTANCE_6_IP "34.102.79.216:5678"
#define INSTANCE_7_IP "34.134.7.170:5678"
#define CRASH_IP "9.9.9.9"

/**
 *
 * instance-5 : 34.125.29.150 - client
 * instance-6 : 34.102.79.216 - primary in the beginning
 * instance-7 : 34.134.7.170 - backup in the beginning
 */
class BlockStorageClient
{

public:
    BlockStorageClient(std::shared_ptr<Channel> channel)
        : stub_(BlockStorage::NewStub(channel)) {}

    void Write(uint64_t address, char *buffer, size_t n)
    {
        // printf("in Write\n");
        if (n != BLOCK_SIZE)
        {
            throw std::runtime_error("Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(n) + ")");
        }
        WriteRequest request;
        WriteResponse reply;
        Status status;
        ClientContext context;

        std::string data_str(buffer, n);

        request.set_address(address);
        request.set_data(data_str);

        // printf("Write: Invoking RPC\n");
        status = stub_->Write(&context, request, &reply);

        if (!status.ok())
        {
            throw std::runtime_error("Status wasn't ok");
        }
    }

    void Read(uint64_t address, char *buffer, size_t n)
    {
        // printf("in Read\n");

        if (n != BLOCK_SIZE)
        {
            throw std::runtime_error("Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(n) + ")");
        }

        ReadRequest request;
        ReadResponse reply;
        Status status;

        ClientContext context;

        request.set_address(address);

        // printf("Read: Invoking RPC\n");

        status = stub_->Read(&context, request, &reply);

        if (!status.ok())
        {
            throw std::runtime_error("Status wasn't ok");
        }
        auto data_str = reply.data();

        if (data_str.length() != BLOCK_SIZE)
        {
            throw std::runtime_error("Received data block of wrong size: should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
        }
        data_str.copy(buffer, n);
        // memcpy(buffer, data_str.data(), n);
    }

private:
    std::unique_ptr<BlockStorage::Stub> stub_;
};

void test(BlockStorageClient &client)
{
    std::string an_input_string("This is tesing!!!");

    char buffer_in[BLOCK_SIZE] = {};
    char buffer_out[BLOCK_SIZE] = {};

    std::memcpy(buffer_in, an_input_string.data(), an_input_string.length());

    uint64_t addr = 760124;
    client.Write(addr, buffer_in, BLOCK_SIZE);
    client.Read(addr, buffer_out, BLOCK_SIZE);

    std::string returned(buffer_out, an_input_string.length());

    std::cout << an_input_string << ":::" << returned << std::endl;

    int k = an_input_string.length();

    cout << "Sent " << std::hex;
    for (int i = 0; i < k; i++)
    {
        cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int)buffer_in[i]) << " ";
    }
    cout << std::dec << endl;

    cout << "Got  " << std::hex;
    for (int i = 0; i < k; i++)
    {
        cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int)buffer_out[i]) << " ";
    }
    cout << std::dec << endl;

    printf("Sent [%s], got back [%s]\n", an_input_string.c_str(), returned.c_str());
}

int main(int argc, char **argv)
{
    std::string target_str(INSTANCE_6_IP);
    BlockStorageClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

    test(client);
    return 0;
}
