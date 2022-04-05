#ifndef FILESTORAGE_H
#define FILESTORAGE_H

#include <shared_mutex>
#include <string>
using std::string;

class FileStorage {
   private:
    string fileName;
    std::mutex mtx;

   public:
    FileStorage(string fileName);

    // initialize this file with 0s.
    // fileSize in MB
    void init(int fileSize);
    void write_data(uint64_t offset, const char *in);
    void read_data(uint64_t offset, char *out);
};

#endif