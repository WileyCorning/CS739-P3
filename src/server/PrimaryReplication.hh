#ifndef PRIMARYREPLICATION_HH
#define PRIMARYREPLICATION_HH

#include "ReplicationModule.hh"
#include "../cmake/build/blockstorage.grpc.pb.h"
#include <string>
#include <thread>
using std::string;
using blockstorageproto::BlockStorage;

class PrimaryReplication : public ReplicationModule
{
private:
    std::unique_ptr<BlockStorage::Stub> stub_;
    std::thread heartbeatThread;
    bool didStart = false;
    
    void HeartbeatLoop();
    
public:
    bool isDone;
    PrimaryReplication(string backupAddr);

    ~PrimaryReplication();

    void PingOnce();

    bool ShouldWriteLocally();
    void AssumeBackupAvailable();
    void AssumeBackupOffline();
    bool HeartbeatOnce();
    void Initialize();
    void TearDown();
    void HandleWriteByClient(uint64_t addr, char *data, size_t length);
};
#endif