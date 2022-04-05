#include "FileStorage.hh"
#include "../shared/CommonDefinitions.hh"
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <string.h>

using std::string;


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

FileStorage::FileStorage(string fileName) : fileName(fileName) {}

// initialize this file with 0s.
// fileSize in MB
void FileStorage::init(int fileSize)
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
void FileStorage::write_data(uint64_t offset, const char *in)
{
    mtx.lock();
    Block block;
    std::ofstream ofs(fileName, std::ios::binary | std::ios::out);
    strcpy(block.data, in);
    ofs.seekp(offset, std::ios::beg);
    ofs.write(reinterpret_cast<char *>(&block), sizeof(block));
    ofs.close();
    mtx.unlock();
}

void FileStorage::read_data(uint64_t offset, char *out)
{
    // Usage example:
    // FileStorage fs("output");
    // char input[] = {'p', '0', 'q', 'v', 'w', '2'};
    // int offset = 762;
    // fs.write_data(offset, input);
    // char output[BLOCK_SIZE];
    // fs.read_data(offset, output);
    
    mtx.lock();
    Block block;
    std::ifstream ifs(fileName, std::ios::binary | std::ios::in);
    ifs.seekg(offset, std::ios::beg);
    ifs.read(reinterpret_cast<char *>(&block), sizeof(block));
    strcpy(out, block.data);
    ifs.close();
    mtx.unlock();
}