#ifndef BACKUPREPLICATION_HH
#define BACKUPREPLICATION_HH

#include "ReplicationModule.hh"
#include "cmake/build/blockstorage.grpc.pb.h"
#include <string>
using std::string;
using blockstorageproto::BlockStorage;

class BackupReplication : public ReplicationModule {
   private:
    std::unique_ptr<BlockStorage::Stub> stub_;

   public:
    BackupReplication(string primaryAddr);
    ~BackupReplication();
    void PingOnce();
    bool ShouldWriteLocally();
    void HandleWriteByClient(uint64_t addr, char *data, size_t length);
};
#endif