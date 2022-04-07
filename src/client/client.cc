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
#include <time.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <iomanip>
#include <random>
#include <vector>
#include <thread>

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
 * ./server/server 5678 standalone fs_1
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

// gererate a string of a specific length
std::string strRand(int length)
{
    char tmp;
    std::string buffer;

    std::random_device rd;
    std::default_random_engine random(rd());

    for (int i = 0; i < length; i++)
    {
        tmp = random() % 36;
        if (tmp < 10)
        {
            tmp += '0';
        }
        else
        {
            tmp -= 10;
            tmp += 'A';
        }
        buffer += tmp;
    }
    return buffer;
}

void writeToZeroOffset(BlockStorageClient *client, char *buffer)
{
    client->Write(0, buffer, BLOCK_SIZE);
}

void consistencyTest(BlockStorageClient &client, std::vector<std::string> &random_strs)
{
    std::vector<std::thread> threads;
    for (size_t i = 0; i < random_strs.size(); i++)
    {
        std::string an_input_string = random_strs[i];
        char buffer_in[BLOCK_SIZE] = {};
        std::memcpy(buffer_in, an_input_string.data(), an_input_string.length());
        threads.push_back(std::thread(writeToZeroOffset, &client, buffer_in));
    }

    for (size_t i = 0; i < threads.size(); i++)
    {
        threads[i].join();
    }

    // returned string should match with one of the strings which was writtern by one thread
    for (size_t i = 0; i < random_strs.size(); i++)
    {
        std::string an_input_string = random_strs[i];
        char buffer_out[BLOCK_SIZE] = {};
        client.Read(0, buffer_out, BLOCK_SIZE);
        std::string returned(buffer_out, an_input_string.length());
        if (returned == an_input_string)
        {
            cout << "Matched with " << i + 1 << "th string" << endl;
            return;
        }
    }
    cout << "No match" << endl;
}

void readWriteBenchmark(BlockStorageClient &client, std::vector<std::string> &random_strs, std::vector<long> &random_offsets)
{
    // std::string an_input_string("This is tesing!!!");
    for (size_t i = 0; i < random_strs.size(); i++)
    {
        std::string an_input_string = random_strs[i];
        char buffer_in[BLOCK_SIZE] = {};
        char buffer_out[BLOCK_SIZE] = {};

        std::memcpy(buffer_in, an_input_string.data(), an_input_string.length());

        uint64_t addr = random_offsets[i];

        auto start = std::chrono::high_resolution_clock::now();
        client.Write(addr, buffer_in, BLOCK_SIZE);
        client.Read(addr, buffer_out, BLOCK_SIZE);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;

        std::string returned(buffer_out, an_input_string.length());

        if (an_input_string == returned)
        {
            cout << "String length: " << an_input_string.length() << " write & read successful! and time spent: " << duration.count() << " ms." << endl;
        }
        else
        {
            cout << "write & read don't match!" << endl;
        }
   }
    // measuring time for 100 writes
    // std::string 4KB_input_string = random_strs[5];
    // char buffer_in_4KB[BLOCK_SIZE] = {};
    // char buffer_out_4KB[BLOCK_SIZE] = {};
    // std::memcpy(buffer_in_4KB, 4KB_input_string.data(), 4KB_input_string.length());

    // uint64_t offset;
    // auto start = std::chrono::high_resolution_clock::now();
    // for (int i = 0; i < 100; i++)
    // {
        
    // client->Write(offset, buffer_in_4KB, BLOCK_SIZE);
    // offset += 4096;
    // }
    //  auto end = std::chrono::high_resolution_clock::now();
    //  std::chrono::duration<double, std::milli> duration = end - start;
    //  cout << "time spent on 100 4KB writes  " << duration.count() << " ms." << endl;
         
    // //measuring time for 100 reads
    
    // offset = 0;
    // auto start = std::chrono::high_resolution_clock::now();
    // for (int i = 0; i < 100; i++)
    // {
        
    // client.Read(offset, buffer_out_4KB, BLOCK_SIZE);
    // offset += 4096;
    // }
    //  auto end = std::chrono::high_resolution_clock::now();
    //  std::chrono::duration<double, std::milli> duration = end - start;
    //  cout << "time spent on 100 4KB reads  " << duration.count() << " ms." << endl;
}

void runTests(BlockStorageClient &client)
{
    std::vector<long> str_lengths = {64, 256, 512, BLOCK_SIZE / 4, BLOCK_SIZE / 2, BLOCK_SIZE};
    std::vector<std::string> random_strs;
    std::vector<long> random_offsets;
    for (auto &&i : str_lengths)
    {
        random_strs.push_back(strRand(i));
    }
    srand((unsigned)time(NULL));
    for (int i = 0; i < str_lengths.size(); i++)
    {
        random_offsets.push_back(rand()); // unaligned
        // random_offsets.push_back(i * BLOCK_SIZE); // 4k-aligned
    }

    readWriteBenchmark(client, random_strs, random_offsets);
    // int testNum = 5;
    // for (size_t i = 0; i < testNum; i++)
    // {
    //     consistencyTest(client, random_strs);
    // }

    // long [9] = {};
    // string
    // long offsets[]

    // int k = an_input_string.length();

    // cout << "Sent " << std::hex;
    // for (int i = 0; i < k; i++)
    // {
    //     cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int)buffer_in[i]) << " ";
    // }
    // cout << std::dec << endl;

    // cout << "Got  " << std::hex;
    // for (int i = 0; i < k; i++)
    // {
    //     cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int)buffer_out[i]) << " ";
    // }
    // cout << std::dec << endl;

    // printf("Sent [%s], got back [%s]\n", an_input_string.c_str(), returned.c_str());
}

int main(int argc, char **argv)
{
    std::string target_str(INSTANCE_6_IP);
    BlockStorageClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    runTests(client);
    return 0;
}
