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
#include <string>
#include <shared_mutex>

#include "cmake/build/blockstorage.grpc.pb.h"

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

class ReplicationModule
{
public:
    ReplicationModule() {}
    virtual ~ReplicationModule() {}
    virtual void PingOnce() = 0;
    virtual bool ShouldWriteLocally() { return true; }
    virtual void HandleWriteByClient(uint64_t addr, char *data, size_t length) = 0;
};

class DiskModule
{
    fs::path root;
    fs::path temp;

    fs::path get_main_path(string address)
    {
        return get_path_safely(root, address);
    }

    fs::path get_temp_path(string address)
    {
        // todo support concurrent write to different tempfiles
        return get_path_safely(temp, address);
    }

    fs::path get_path_safely(fs::path parent, string address)
    {
        auto normalized = (parent / address).lexically_normal();

        // Check that this path is under our storage root
        auto [a, b] = std::mismatch(parent.begin(), parent.end(), normalized.begin());
        if (a != parent.end())
        {
            throw std::runtime_error("Normalized path is outside storage root");
        }

        return normalized;
    }

public:
    DiskModule(fs::path root, fs::path temp) : root(root), temp(temp) {}

    void write_data(uint64_t address, string data)
    {
        // TODO replace with pwrite in a single file
        auto stable = get_main_path(std::to_string(address));
        auto transient = get_temp_path(std::to_string(address));

        std::ofstream file;
        file.open(transient, std::ios::binary); // todo handle file.fail()
        file << data;
        file.close();

        rename(transient.c_str(), stable.c_str()); // todo handle err
    }

    string read_data(uint64_t address)
    {
        // TODO replace with pread in a single file
        auto stable = get_main_path(std::to_string(address));

        cout << "Addr " << address << " to " << stable << endl;

        char *buffer = new char[BLOCK_SIZE]{};

        if (fs::exists(stable))
        {
            std::ifstream file(stable, std::ios::in | std::ios::binary);
            file.read(buffer, BLOCK_SIZE); // todo handle file.fail()
            if (file.fail())
            {
                cout << "Error reading file at " << stable.string() << endl;
            }
        }
        else
        {
            cout << "No file at " << stable.string() << endl;
        }

        cout << "Read  " << std::hex;
        for (int i = 0; i < 32; i++)
        {
            cout << std::setfill('0') << std::setw(2) << (0xff & (unsigned int)buffer[i]) << " ";
        }
        cout << std::dec << endl;

        return string(buffer, BLOCK_SIZE);
    }
};

/**
 * All blocks are stored within this large file at some specific offsets.
 * The first time a server starts, it should initialize this file with 0s.
 * Writes are protected by the lock.
 *
 * My ideas on crash & recovery:
 * If one server crashes, the other one could use a map structure to keep track of new writes <offset, 4k block>,
 * so that after that server recovers from the crash, it retrieves and replays all the writes in the map and files will be identical again.
 * I think crash during writes is trivial, since the client could just direct the write to the other server and that broken block will get overwritten later.
 */
struct Block
{
    char data[BLOCK_SIZE];
};

class FileStorage
{
    string fileName;
    std::mutex mtx;

public:
    FileStorage(string fileName) : fileName(fileName) {}

    // initialize this file with 0s.
    // fileSize in MB
    void init(int fileSize)
    {
        std::vector<char> empty(1024, 0);
        std::ofstream ofs(fileName, std::ios::binary | std::ios::out);

        for (int i = 0; i < 1024 * fileSize; i++)
        {
            if (!ofs.write(&empty[0], empty.size()))
            {
                std::cerr << "problem writing to file" << std::endl;
            }
        }
        ofs.close();
    }

    // there's no need to handle crash during writes
    void write_data(uint64_t offset, char data[])
    {
        mtx.lock();
        Block block;
        std::ofstream ofs(fileName, std::ios::binary | std::ios::out);
        strcpy(block.data, data);
        ofs.seekp(offset, std::ios::beg);
        ofs.write(reinterpret_cast<char *>(&block), sizeof(block));
        ofs.close();
        mtx.unlock();
    }

    void read_data(uint64_t offset, char *out)
    {
        mtx.lock();
        Block block;
        std::ifstream ifs(fileName, std::ios::binary | std::ios::in);
        ifs.seekg(offset, std::ios::beg);
        ifs.read(reinterpret_cast<char *>(&block), sizeof(block));
        strcpy(out, block.data);
        ifs.close();
        mtx.unlock();
    }
};

// Logic and data behind the server's behavior.
class BlockStorageServiceImpl final : public BlockStorage::Service
{
    DiskModule disk;
    ReplicationModule *replication;

    Status Ping(ServerContext *context, const PingMessage *request, PingMessage *reply) override
    {
        return Status::OK;
    }

    Status Read(ServerContext *context, const ReadRequest *request, ReadResponse *reply) override
    {
        reply->set_data(disk.read_data(request->address()));
        return Status::OK;
    }

    Status Write(ServerContext *context, const WriteRequest *request, WriteResponse *reply) override
    {
        auto data_str = request->data();
        if (data_str.length() != BLOCK_SIZE)
        {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "Block size should be " + std::to_string(BLOCK_SIZE) + " (was " + std::to_string(data_str.length()) + ")");
        }

        disk.write_data(request->address(), request->data());

        return Status::OK;
    }

public:
    BlockStorageServiceImpl(DiskModule disk, ReplicationModule *replication) : disk(disk), replication(replication) {}
};

void RunServer(string port, DiskModule disk, ReplicationModule *replication)
{
    string server_address = "0.0.0.0:" + port; // "0.0.0.0:50051"
    BlockStorageServiceImpl service(disk, replication);

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

    replication->PingOnce();

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

class NoReplication : public ReplicationModule
{
public:
    NoReplication() {}
    ~NoReplication() {}
    void PingOnce() {}
    bool ShouldWriteLocally() { return true; }
    void HandleWriteByClient(uint64_t addr, char *data, size_t length) {}
};

class PrimaryReplication : public ReplicationModule
{
private:
    std::unique_ptr<BlockStorage::Stub> stub_;

public:
    PrimaryReplication(string backupAddr)
    {
        cout << "Initializing replication as Primary; Backup at " << backupAddr << endl;
        stub_ = BlockStorage::NewStub(grpc::CreateChannel(backupAddr, grpc::InsecureChannelCredentials()));
    }

    ~PrimaryReplication() {}

    void PingOnce()
    {
        PingMessage request;
        PingMessage reply;
        Status status;
        uint32_t retryCount = 0;

        // Retry w backoff
        do
        {
            ClientContext context;
            cout << "Attempting to ping the backup..." << endl;
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

    bool ShouldWriteLocally()
    {
        return true; // TODO wait to write locally if currently reintegrating
    }

    void HandleWriteByClient(uint64_t addr, char *data, size_t length)
    {
        // TODO send to backup
    }
};

class BackupReplication : public ReplicationModule
{
private:
    std::unique_ptr<BlockStorage::Stub> stub_;

public:
    BackupReplication(string primaryAddr)
    {
        cout << "Initializing replication as Backup; Primary at " << primaryAddr << endl;
        stub_ = BlockStorage::NewStub(grpc::CreateChannel(primaryAddr, grpc::InsecureChannelCredentials()));
    }

    ~BackupReplication() {}

    void PingOnce()
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

    bool ShouldWriteLocally()
    {
        return false; // TODO write locally if primary is offline
    }

    void HandleWriteByClient(uint64_t addr, char *data, size_t length)
    {
        // TODO forward to primary if it is online
    }
};

string argErrString(string name)
{
    return "Usage: " + name + " <port> ( standalone | primary --backup-address <backup-address> | backup --primary-address <primary-address> ) <root_folder> <temp_folder>";
}

ReplicationModule *GetReplicationModule(string name, char **argv, size_t idx)
{
    auto value = string(argv[idx]);
    if (value == "standalone")
        return new NoReplication();

    auto flag = string(argv[idx + 1]);
    auto target = string(argv[idx + 2]);

    if (value == "primary")
    {
        if (flag != "--backup-address")
        {
            throw std::runtime_error(argErrString(name));
        }
        return new PrimaryReplication(target);
    }

    if (value == "backup")
    {
        if (flag != "--primary-address")
        {
            throw std::runtime_error(argErrString(name));
        }
        return new BackupReplication(target);
    }

    throw std::runtime_error(argErrString(name));
}

int main(int argc, char **argv)
{
    string name = argv[0];

    if (argc != 5 && argc != 7)
    {
        cout << argErrString(name) << endl;
        return 1;
    }

    string port = argv[1];

    auto is_standalone = string(argv[2]) == "standalone";
    size_t dir_idx = is_standalone ? 3 : 5;

    auto root = fs::canonical(string(argv[dir_idx]));
    auto temp = fs::canonical(string(argv[dir_idx + 1]));

    cout << "Serving files from " << root << "; temp is " << temp << endl;

    auto repl = GetReplicationModule(name, argv, 2);

    RunServer(port, DiskModule(root, temp), repl);
    return 0;
}
