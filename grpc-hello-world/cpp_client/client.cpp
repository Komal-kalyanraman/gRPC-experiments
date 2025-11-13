#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "helloworld.grpc.pb.h"

int main(int argc, char** argv) {
    std::string target_str = "localhost:50051";
    std::string user = "World";
    if (argc > 1) user = argv[1];

    grpc::ChannelArguments ch_args;
    auto channel = grpc::CreateCustomChannel(target_str, grpc::InsecureChannelCredentials(), ch_args);
    std::unique_ptr<helloworld::Greeter::Stub> stub = helloworld::Greeter::NewStub(channel);

    helloworld::HelloRequest request;
    request.set_name(user);

    helloworld::HelloReply reply;
    grpc::ClientContext context;
    grpc::Status status = stub->SayHello(&context, request, &reply);

    if (!status.ok()) {
        std::cerr << "RPC failed: code=" << status.error_code()
                  << " message=" << status.error_message() << std::endl;
        return 1;
    }

    std::cout << "Greeter client received: " << reply.message() << std::endl;
    return 0;
}