#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <fstream>
#include <sstream>

#include <grpcpp/grpcpp.h>
#include "network.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;
using network::NodeMetrics;
using network::MetricsAck;
using network::NetworkMonitoring;

class NetworkMonitoringClient {
private:
    std::unique_ptr<NetworkMonitoring::Stub> stub_;
    std::string node_id_;

    void GetNodeStats(const std::string& interface, 
                      uint64_t& rx_packets, uint64_t& rx_bytes,
                      uint64_t& tx_packets, uint64_t& tx_bytes,
                      uint32_t& rx_errors, uint32_t& tx_errors) {
        // Read from /proc/net/dev
        std::ifstream dev_file("/proc/net/dev");
        std::string line;
        
        while (std::getline(dev_file, line)) {
            if (line.find(interface) != std::string::npos) {
                std::istringstream iss(line);
                std::string iface;
                iss >> iface; // interface name with colon
                
                // Read: rx_bytes rx_packets rx_errs rx_drop ... tx_bytes tx_packets tx_errs ...
                uint64_t dummy;
                iss >> rx_bytes >> rx_packets >> rx_errors;
                for (int i = 0; i < 5; ++i) iss >> dummy; // skip
                iss >> tx_bytes >> tx_packets >> tx_errors;
                return;
            }
        }
        
        // Defaults if not found
        rx_packets = rx_bytes = tx_packets = tx_bytes = 0;
        rx_errors = tx_errors = 0;
    }

    bool PopulateNodeMetrics(NodeMetrics* metrics) {
        metrics->set_node_id(node_id_);
        
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        
        metrics->mutable_timestamp()->set_seconds(seconds.count());

        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == -1) {
            return false;
        }

        bool found = false;
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            if (std::string(ifa->ifa_name) != "wlp0s20f3") continue;

            found = true;
            metrics->set_interface_name(ifa->ifa_name);
            metrics->set_mtu(1500);

            // Flags
            if (ifa->ifa_flags & IFF_UP) metrics->add_flags("UP");
            if (ifa->ifa_flags & IFF_BROADCAST) metrics->add_flags("BROADCAST");
            if (ifa->ifa_flags & IFF_RUNNING) metrics->add_flags("RUNNING");
            if (ifa->ifa_flags & IFF_MULTICAST) metrics->add_flags("MULTICAST");

            // IP addresses
            int family = ifa->ifa_addr->sa_family;
            char host[NI_MAXHOST];
            
            if (family == AF_INET) {
                getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                           host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                metrics->set_ipv4(host);

                if (ifa->ifa_netmask) {
                    char netmask[NI_MAXHOST];
                    getnameinfo(ifa->ifa_netmask, sizeof(struct sockaddr_in),
                               netmask, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                    metrics->set_netmask(netmask);
                }
                
                if (ifa->ifa_broadaddr) {
                    char broadcast[NI_MAXHOST];
                    getnameinfo(ifa->ifa_broadaddr, sizeof(struct sockaddr_in),
                               broadcast, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                    metrics->set_broadcast(broadcast);
                }
            } else if (family == AF_INET6) {
                getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
                           host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                metrics->add_ipv6(host);
            }
        }

        freeifaddrs(ifaddr);

        if (!found) {
            return false;
        }

        // Get packet statistics
        uint64_t rx_packets, rx_bytes, tx_packets, tx_bytes;
        uint32_t rx_errors, tx_errors;
        GetNodeStats("wlp0s20f3", rx_packets, rx_bytes, tx_packets, tx_bytes, 
                     rx_errors, tx_errors);
        
        metrics->set_rx_packets(rx_packets);
        metrics->set_rx_bytes(rx_bytes);
        metrics->set_tx_packets(tx_packets);
        metrics->set_tx_bytes(tx_bytes);
        metrics->set_rx_errors(rx_errors);
        metrics->set_tx_errors(tx_errors);
        
        // MAC address placeholder
        metrics->set_mac("70:cf:49:b8:48:72");

        return true;
    }

public:
    NetworkMonitoringClient(std::shared_ptr<Channel> channel, const std::string& node_id)
        : stub_(NetworkMonitoring::NewStub(channel)), node_id_(node_id) {}

    void StreamNodeMetrics(int interval_seconds = 5, int duration_seconds = 300) {
        ClientContext context;
        
        std::shared_ptr<ClientReaderWriter<NodeMetrics, MetricsAck>> stream(
            stub_->StreamNodeMetrics(&context));

        std::cout << "[Client] Starting node metrics stream for node: " << node_id_ << std::endl;

        // Sending thread
        std::thread send_thread([this, stream, interval_seconds, duration_seconds]() {
            auto start = std::chrono::steady_clock::now();
            int metrics_count = 0;

            while (true) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start);

                if (elapsed.count() >= duration_seconds) {
                    std::cout << "[Client] Duration expired, closing stream" << std::endl;
                    stream->WritesDone();
                    break;
                }

                NodeMetrics metrics;
                if (!PopulateNodeMetrics(&metrics)) {
                    std::cerr << "[Client] Failed to get node metrics" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
                    continue;
                }

                std::cout << "[Client] Sending node metrics (batch #" << ++metrics_count << ")" 
                          << std::endl;

                if (!stream->Write(metrics)) {
                    std::cerr << "[Client] Failed to write metrics" << std::endl;
                    break;
                }

                std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
            }
        });

        // Receiving thread
        MetricsAck ack;
        while (stream->Read(&ack)) {
            std::cout << "[Client] Received ACK: " << ack.message() 
                      << " (timestamp: " << ack.server_timestamp().seconds() << ")"
                      << std::endl;
        }

        send_thread.join();

        Status status = stream->Finish();
        if (status.ok()) {
            std::cout << "[Client] Streaming completed successfully" << std::endl;
        } else {
            std::cerr << "[Client] Error: " << status.error_code() << " - "
                      << status.error_message() << std::endl;
            std::cerr << "[Client] Connection to server lost or closed unexpectedly." << std::endl;
        }
    }
};

int main(int argc, char** argv) {
    std::string target_str("localhost:50051");
    std::string node_id("node-01");

    if (argc > 1) {
        node_id = argv[1];
    }

    auto channel = grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials());
    NetworkMonitoringClient client(channel, node_id);

    std::cout << "=== gRPC Network Monitoring Client ===" << std::endl;
    std::cout << "Node ID: " << node_id << std::endl;
    std::cout << "Server: " << target_str << std::endl;

    // Stream node metrics every 5 seconds for 5 minutes
    client.StreamNodeMetrics(5, 300);

    return 0;
}