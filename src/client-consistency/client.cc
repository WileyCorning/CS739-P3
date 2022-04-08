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
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../cmake/build/blockstorage.grpc.pb.h"
#include "../shared/CommonDefinitions.hh"

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
#define INSTANCE_5_IP "34.125.29.150:5678"
#define INSTANCE_6_IP "34.102.79.216:5678"
#define INSTANCE_7_IP "34.134.7.170:5678"
#define CRASH_IP "9.9.9.9"

/**
 *
 * instance-5 : 34.125.29.150 - client
 * instance-6 : 34.102.79.216 - primary in the beginning
 * instance-7 : 34.134.7.170 - backup in the beginning
 * standalone: ./server/server 5678 primary --backup-address 0  fs_1
 *
 * two servers
 * on instance-5:  ./server/server 5678 primary --backup-address 34.102.79.216:5678  fs_1
 * on instance-6: ./server/server 5678  backup --primary-address 34.125.29.150:5678  fs_1
 */
class BlockStorageClient {
   public:
    BlockStorageClient(std::shared_ptr<Channel> channel_primary, std::shared_ptr<Channel> channel_backup)
        : stub_primary(BlockStorage::NewStub(channel_primary)), stub_backup(BlockStorage::NewStub(channel_backup)) {}

    void Write(uint64_t address, const char *buffer, size_t n) {
        // printf("in Write\n");
        if (n != BLOCK_SIZE) {
            throw std::runtime_error("Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(n) + ")");
        }
        Status status;

        std::string data_str(buffer, n);

        do {
            ClientContext context;
            WriteRequest request;
            WriteResponse reply;
            request.set_address(address);
            request.set_data(data_str);

            if (use_backup) {
                printf("Write (to Backup)\n");
                status = stub_backup->Write(&context, request, &reply);
            } else {
                printf("Write (to Primary)\n");
                status = stub_primary->Write(&context, request, &reply);
            }
            if (!status.ok()) {
                use_backup = !use_backup;
            }
        } while (!status.ok());
    }

    void Read(uint64_t address, char *buffer, size_t n) {
        if (n != BLOCK_SIZE) {
            throw std::runtime_error("Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(n) + ")");
        }

        Status status;


        ReadResponse reply;

        do {
            ReadRequest request;
            request.set_address(address);
            ClientContext context;
            if (use_backup) {
                printf("Read (from Backup)\n");
                status = stub_backup->Read(&context, request, &reply);
            } else {
                printf("Read (from Primary)\n");
                status = stub_primary->Read(&context, request, &reply);
            }
            if (!status.ok()) {
                use_backup = !use_backup;
            }
        } while (!status.ok());

        auto data_str = reply.data();

        if (data_str.length() != BLOCK_SIZE) {
            throw std::runtime_error("Received data block of wrong size: should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
        }
        data_str.copy(buffer, n);
    }

   private:
    std::unique_ptr<BlockStorage::Stub> stub_primary;
    std::unique_ptr<BlockStorage::Stub> stub_backup;
    bool use_backup = false;
};

// gererate a string of a specific length
std::string strRand(int length) {
    char tmp;
    std::string buffer;

    std::random_device rd;
    std::default_random_engine random(rd());

    for (int i = 0; i < length; i++) {
        tmp = random() % 36;
        if (tmp < 10) {
            tmp += '0';
        } else {
            tmp -= 10;
            tmp += 'A';
        }
        buffer += tmp;
    }
    return buffer;
}

void writeToZeroOffset(BlockStorageClient *client, char *buffer) {
    client->Write(0, buffer, BLOCK_SIZE);
}

void consistencyTest(BlockStorageClient &client, std::vector<std::string> &random_strs) {
    std::vector<std::thread> threads;
    for (size_t i = 0; i < random_strs.size(); i++) {
        std::string an_input_string = random_strs[i];
        char buffer_in[BLOCK_SIZE] = {};
        std::memcpy(buffer_in, an_input_string.data(), an_input_string.length());
        threads.push_back(std::thread(writeToZeroOffset, &client, buffer_in));
    }

    for (size_t i = 0; i < threads.size(); i++) {
        threads[i].join();
    }

    // returned string should match with one of the strings which was writtern by one thread
    for (size_t i = 0; i < random_strs.size(); i++) {
        std::string an_input_string = random_strs[i];
        char buffer_out[BLOCK_SIZE] = {};
        client.Read(0, buffer_out, BLOCK_SIZE);
        std::string returned(buffer_out, an_input_string.length());
        if (returned == an_input_string) {
            cout << "Matched with " << i + 1 << "th string" << endl;
            return;
        }
    }
    cout << "No match" << endl;
}

void readWriteBenchmark(BlockStorageClient &client, std::vector<std::string> &random_strs, std::vector<long> &random_offsets) {
    // std::string an_input_string("This is tesing!!!");
    for (size_t i = 0; i < random_strs.size(); i++) {
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

        if (an_input_string == returned) {
            cout << "String length: " << an_input_string.length() << " write & read successful! and time spent: " << duration.count() << " ms." << endl;
        } else {
            cout << "write & read don't match!" << endl;
        }
    }
    // measuring time for 100 writes
    //  std::string KB_input_string = random_strs[5];
    //  char buffer_in_4KB[BLOCK_SIZE] = {};
    //  char buffer_out_4KB[BLOCK_SIZE] = {};
    //  std::memcpy(buffer_in_4KB, KB_input_string.data(), KB_input_string.length());

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

    // measuring time for 100 reads

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


void run_main(BlockStorageClient *client, uint64_t prep, uint64_t target) {
    for (int i = 0; i < 10; i++) {
        auto payload1 = strRand(BLOCK_SIZE);
        auto payload2 = strRand(BLOCK_SIZE);
        char buffer_out[BLOCK_SIZE] = {};

        client->Write(prep, payload1.c_str(), BLOCK_SIZE);
        auto start = std::chrono::high_resolution_clock::now();
        client->Write(target, payload2.c_str(), BLOCK_SIZE);
        client->Read(target, buffer_out, BLOCK_SIZE);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;

        std::string returned(buffer_out, payload2.length());

        if (payload2 == returned) {
            cout << "[iter " << i << "] OK: took " << duration.count() << "ms" << endl;
        } else {
            cout << "[iter " << i << "] inconsistent!" << endl;
        }
    }
}

void run_rest(BlockStorageClient *client, uint64_t prep, uint64_t target) {
    for (int i = 0; i < 10; i++) {
        auto payload1 = strRand(BLOCK_SIZE);
        auto payload2 = strRand(BLOCK_SIZE);
        auto payload3 = strRand(BLOCK_SIZE);
        char buffer_out[BLOCK_SIZE] = {};

        client->Write(PREP_CRASH_ON_NEXT_RECOVER, payload1.c_str(), BLOCK_SIZE);
        client->Write(prep, payload2.c_str(), BLOCK_SIZE);
        auto start = std::chrono::high_resolution_clock::now();
        client->Write(target, payload2.c_str(), BLOCK_SIZE);
        client->Read(target, buffer_out, BLOCK_SIZE);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;

        std::string returned(buffer_out, payload2.length());

        if (payload2 == returned) {
            cout << "[iter " << i << "] OK: took " << duration.count() << "ms" << endl;
        } else {
            cout << "[iter " << i << "] inconsistent!" << endl;
        }
    }
}


void seq0(BlockStorageClient *client) {
    cout << "Crashing primary during write, before calling backup" << endl;
    run_main(client,PREP_CRASH_ON_MESSAGE_PRIMARY, CRASH_PRIMARY_BEFORE_BACKUP);
}

void seq1(BlockStorageClient* client) {
    cout << "Crashing primary after write" << endl;
    run_main(client,PREP_CRASH_ON_MESSAGE_PRIMARY, CRASH_PRIMARY_AFTER_WRITE);
}

void seq2(BlockStorageClient *client) {
    cout << "Crashing backup during backup" << endl;
    run_main(client,PREP_CRASH_ON_MESSAGE_BACKUP, CRASH_BACKUP_DURING_BACKUP);
}

void seq3(BlockStorageClient *client) {
    cout << "Crashing backup after backup" << endl;
    run_main(client,PREP_CRASH_ON_MESSAGE_BACKUP, CRASH_BACKUP_AFTER_BACKUP);
}

void seq4(BlockStorageClient *client) {
    cout << "Crashing primary once, then again as it recovers" << endl;
    run_rest(client,PREP_CRASH_ON_MESSAGE_PRIMARY, CRASH_PRIMARY_BEFORE_BACKUP);
}

void seq5(BlockStorageClient *client) {
    cout << "Crashing backup once, then again as it recovers" << endl;
    run_rest(client,PREP_CRASH_ON_MESSAGE_BACKUP, CRASH_BACKUP_DURING_BACKUP);
}

void runTests(BlockStorageClient &client) {
    // std::vector<long> str_lengths = {64, 256, 512, BLOCK_SIZE / 4, BLOCK_SIZE / 2, BLOCK_SIZE};
    std::vector<long> str_lengths = {BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE};
    std::vector<std::string> random_strs;
    std::vector<long> random_offsets;
    for (auto &&i : str_lengths) {
        random_strs.push_back(strRand(i));
    }
    srand((unsigned)time(NULL));
    for (int i = 0; i < str_lengths.size(); i++) {
        // random_offsets.push_back(rand()); // unaligned
        random_offsets.push_back(i * BLOCK_SIZE);  // 4k-aligned
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

int main(int argc, char **argv) {
    std::string target_str_1(INSTANCE_5_IP);
    std::string target_str_2(INSTANCE_6_IP);

    BlockStorageClient client(
        grpc::CreateChannel(target_str_1, grpc::InsecureChannelCredentials()),
        grpc::CreateChannel(target_str_2, grpc::InsecureChannelCredentials()));
    
    auto n = atoi(argv[1]);
    switch(n) {
        case 0: seq0(&client);break;
        case 1: seq1(&client);break;
        case 2: seq2(&client);break;
        case 3: seq3(&client);break;
        case 4: seq4(&client);break;
        case 5: seq5(&client);break;
        default: return 1;
    }
    
    return 0;
}
