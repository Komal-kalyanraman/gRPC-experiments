#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "network.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using network::NetworkInfo;
using network::WifiInfoRequest;
using network::WifiInfoReply;
using network::WifiInterface;

class NetworkInfoServiceImpl final : public NetworkInfo::Service {
  Status SendWifiInfo(ServerContext* context, const WifiInfoRequest* request,
                      WifiInfoReply* reply) override {
    const WifiInterface& wifi = request->wifi();
    std::cout << "Received Wi-Fi details from client:" << std::endl;
    std::cout << "  Name: " << wifi.name() << std::endl;
    std::cout << "  Flags: ";
    for (const auto& flag : wifi.flags()) std::cout << flag << " ";
    std::cout << "\n  MTU: " << wifi.mtu() << std::endl;
    std::cout << "  IPv4: " << wifi.ipv4() << std::endl;
    std::cout << "  Netmask: " << wifi.netmask() << std::endl;
    std::cout << "  Broadcast: " << wifi.broadcast() << std::endl;
    std::cout << "  MAC: " << wifi.mac() << std::endl;
    std::cout << "  RX packets: " << wifi.rx_packets() << std::endl;
    std::cout << "  RX bytes: " << wifi.rx_bytes() << std::endl;
    std::cout << "  TX packets: " << wifi.tx_packets() << std::endl;
    std::cout << "  TX bytes: " << wifi.tx_bytes() << std::endl;
    std::cout << "  IPv6: ";
    for (const auto& ip6 : wifi.ipv6()) std::cout << ip6 << " ";
    std::cout << std::endl;

    reply->set_status("Wi-Fi info received");
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  NetworkInfoServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  return 0;
}