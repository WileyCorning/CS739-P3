#ifndef REPLICATIONMODULE_HH
#define REPLICATIONMODULE_HH

#include <memory>

class ReplicationModule
{
public:
    ReplicationModule() {}
    virtual ~ReplicationModule() {}
    virtual void PingOnce() {}
    virtual bool ShouldWriteLocally() { return true; }
    virtual void Initialize() {}
    virtual void TearDown() {}
    virtual void HandleWriteByClient(uint64_t addr, char *data, size_t length) {}
};
#endif