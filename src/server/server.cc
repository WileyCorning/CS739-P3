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

void RunServer(PairedServer* service, string port) {
    string server_address = "0.0.0.0:" + port;

    // GRPC boilerplate
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    
    
    std::cout << "Server listening on " << server_address << std::endl;
    
    if(service->SafeGetState() == ReplState::Recovering) {
        // Start recovery task on new thread
        std::thread([service] { service->Recover(); }).detach();
    }
    
    // Block until shutdown
    server->Wait();
}

string argErrString(string name) {
    return "Usage: " + name + " <port> ( primary --backup-address <backup-address> | backup --primary-address <primary-address> ) <storage_file> [--recover]";
}

// Polymorphic server factory
PairedServer* MakeServer(string name, bool recovering, string kind, FileStorage* storage, std::shared_ptr<grpc::Channel> partnerChannel) {
    auto replication = new ReplicationModule(partnerChannel);
    if(kind == "primary") {
        // Initialize the primary in Standalone mode, unless we're recovering
        return new PrimaryServer(recovering ? ReplState::Recovering : ReplState::Standalone,storage,replication);
    }
    if (kind == "backup") {
        // Wait for the primary to come online before starting the backup
        replication->PingOnce();
        auto heartbeat = new HeartbeatHelper(partnerChannel);
        
        // Always start backup as recovering.
        // This will handle any updates that were serviced by the primary prior to the backup coming online.
        return new BackupServer(ReplState::Recovering,storage,replication,heartbeat);
    }
    throw std::runtime_error(argErrString(name));
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
    
    // Construct channel to other server
    auto partnerChannel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    
    auto server = MakeServer(name,is_recover,kind,&storage,partnerChannel);

    RunServer(server,port);
    return 0;
}
