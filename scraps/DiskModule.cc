
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <shared_mutex>
#include <thread>

namespace fs = std::filesystem;
using std::cout;
using std::endl;
using std::string;

#define BLOCK_SIZE 4096
#define WHOLE_FILE "whole_file"

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
