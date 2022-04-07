#ifndef REPLICATIONMODULE_HH
#define REPLICATIONMODULE_HH

#include <grpcpp/grpcpp.h>

#include <memory>

#include "../cmake/build/blockstorage.grpc.pb.h"
using blockstorageproto::BlockStorage;
#include <mutex>
#include <shared_mutex>

#include "FileStorage.hh"

class ReplicationModule {
   private:
    std::unordered_set<uint64_t> dirtySet;
    std::vector<uint64_t> dirtyVec;
    std::unique_ptr<BlockStorage::Stub> stub_;
   public:
    ReplicationModule(std::shared_ptr<grpc::Channel> channel);

    void PingOnce();
    void MarkDirty(uint64_t address);
    void ClearDirty();
    bool TrySendBackupWrite(uint64_t address, const char* buffer);
    bool TrySendTriggerSync(int sync_id);
    bool TrySendSyncBlock(int sync_id, uint64_t address, char* buffer);
    bool TrySendFinishSync(int sync_id, size_t block_count);
    bool TryPerformSync(int sync_id, std::unique_lock<std::shared_mutex>* lock, FileStorage* storage);
};

#endif