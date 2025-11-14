#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <grpcpp/grpcpp.h>
#include "network.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using network::NetworkInfo;
using network::WifiInfoRequest;
using network::WifiInfoReply;
using network::WifiInterface;

class NetworkInfoClient {
 public:
  NetworkInfoClient(std::shared_ptr<Channel> channel)
      : stub_(NetworkInfo::NewStub(channel)) {}

  void SendWifiInfo() {
    WifiInfoRequest request;
    FillWifiInterface(request.mutable_wifi());

    WifiInfoReply reply;
    ClientContext context;

    Status status = stub_->SendWifiInfo(&context, request, &reply);

    if (status.ok()) {
      std::cout << "Server reply: " << reply.status() << std::endl;
    } else {
      std::cout << "RPC failed: " << status.error_code() << ": " 
                << status.error_message() << std::endl;
    }
  }

 private:
  std::unique_ptr<NetworkInfo::Stub> stub_;

  void FillWifiInterface(WifiInterface* wifi) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == NULL) continue;
      if (std::string(ifa->ifa_name) != "wlp0s20f3") continue;

      wifi->set_name(ifa->ifa_name);
      wifi->set_mtu(1500); // You can fetch actual MTU if needed

      // Flags
      if (ifa->ifa_flags & IFF_UP) wifi->add_flags("UP");
      if (ifa->ifa_flags & IFF_BROADCAST) wifi->add_flags("BROADCAST");
      if (ifa->ifa_flags & IFF_RUNNING) wifi->add_flags("RUNNING");
      if (ifa->ifa_flags & IFF_MULTICAST) wifi->add_flags("MULTICAST");

      // IP addresses
      int family = ifa->ifa_addr->sa_family;
      char host[NI_MAXHOST];
      if (family == AF_INET) {
        getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                    host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        wifi->set_ipv4(host);

        if (ifa->ifa_netmask) {
          char netmask[NI_MAXHOST];
          getnameinfo(ifa->ifa_netmask, sizeof(struct sockaddr_in),
                      netmask, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
          wifi->set_netmask(netmask);
        }
        if (ifa->ifa_broadaddr) {
          char broadcast[NI_MAXHOST];
          getnameinfo(ifa->ifa_broadaddr, sizeof(struct sockaddr_in),
                      broadcast, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
          wifi->set_broadcast(broadcast);
        }
      } else if (family == AF_INET6) {
        getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        wifi->add_ipv6(host);
      }

      // MAC address (not available via getifaddrs, would need ioctl/SIOCGIFHWADDR)
      wifi->set_mac("unknown");

      // Packet stats (not available via getifaddrs, would need /proc/net/dev or ioctl)
      wifi->set_rx_packets(0);
      wifi->set_rx_bytes(0);
      wifi->set_tx_packets(0);
      wifi->set_tx_bytes(0);
    }

    freeifaddrs(ifaddr);
  }
};

int main(int argc, char** argv) {
  NetworkInfoClient client(
      grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));
  client.SendWifiInfo();
  return 0;
}