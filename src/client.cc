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

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <utime.h>
#include <fstream>
#include <sstream>

#include "blockstorage.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using blockstorageproto::BlockStorage;
using blockstorageproto::PingMessage;
using blockstorageproto::ReadRequest;
using blockstorageproto::ReadResponse;
using blockstorageproto::WriteRequest;
using blockstorageproto::WriteResponse;

class BlockStorageClient {
 public:
  BlockStorageClient(std::shared_ptr<Channel> channel)
      : stub_(BlockStorage::NewStub(channel)) {}

            int Ping(std::chrono::nanoseconds * round_trip_time) {
                auto start =std::chrono::steady_clock::now();
                
                PingMessage request;
                PingMessage reply;
                Status status;
                uint32_t retryCount = 0;
                
                // Retry w backoff
                ClientContext context;
                printf("Ping: Invoking RPC\n");
                status = stub_->Ping(&context, request, &reply);

                // Checking RPC Status
                if (status.ok()) 
                {
                    
                    printf("Ping: RPC Success\n");
                    auto end =std::chrono::steady_clock::now();
                    *round_trip_time = end-start;
                    #if DEBUG
                    std::chrono::duration<double,std::ratio<1,1>> seconds = end-start;
                    printf("Ping: Exiting function (took %fs)\n",seconds.count());
                    #endif
                    return 0;
                }
                else
                {
                    printf("Ping: RPC failure\n");
                    printf("Ping: Exiting function\n");
                    return -1;
                }
                
                
            }

 private:
  std::unique_ptr<BlockStorage::Stub> stub_;
};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  std::string target_str;
  std::string arg_str("--target");
  if (argc > 1) {
    std::string arg_val = argv[1];
    size_t start_pos = arg_val.find(arg_str);
    if (start_pos != std::string::npos) {
      start_pos += arg_str.size();
      if (arg_val[start_pos] == '=') {
        target_str = arg_val.substr(start_pos + 1);
      } else {
        std::cout << "The only correct argument syntax is --target="
                  << std::endl;
        return 0;
      }
    } else {
      std::cout << "The only acceptable argument is --target=" << std::endl;
      return 0;
    }
  } else {
    target_str = "localhost:50051";
  }
  BlockStorageClient greeter(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  std::string user("world");
  
  std::chrono::nanoseconds ping_time;
  greeter.Ping(&ping_time);

  return 0;
}
