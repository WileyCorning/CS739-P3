#ifndef NOREPLICATION_HH
#define NOREPLICATION_HH

#include "ReplicationModule.hh"

class NoReplication : public ReplicationModule {
   public:
    NoReplication() {}
    ~NoReplication() {}
};

#endif