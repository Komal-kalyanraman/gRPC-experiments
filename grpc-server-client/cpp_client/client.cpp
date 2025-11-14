#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "network.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;
using network::NodeMetrics;
using network::MetricsAck;
using network::NetworkMonitoring;

// Node template struct
struct NodeTemplate {
    std::string interface_name;
    std::string ipv4;
    std::string netmask;
    std::string broadcast;
    std::vector<std::string> ipv6;
    std::string mac;
    uint32_t mtu;
};

// 10 node templates
static const NodeTemplate node_templates[10] = {
    {"ifc-01", "10.0.0.1", "255.255.255.0", "10.0.0.255", {"fe80::1"}, "AA:BB:CC:DD:EE:01", 1500},
    {"ifc-02", "10.0.0.2", "255.255.255.0", "10.0.0.255", {"fe80::2"}, "AA:BB:CC:DD:EE:02", 1500},
    {"ifc-03", "10.0.0.3", "255.255.255.0", "10.0.0.255", {"fe80::3"}, "AA:BB:CC:DD:EE:03", 1500},
    {"ifc-04", "10.0.0.4", "255.255.255.0", "10.0.0.255", {"fe80::4"}, "AA:BB:CC:DD:EE:04", 1500},
    {"ifc-05", "10.0.0.5", "255.255.255.0", "10.0.0.255", {"fe80::5"}, "AA:BB:CC:DD:EE:05", 1500},
    {"ifc-06", "10.0.0.6", "255.255.255.0", "10.0.0.255", {"fe80::6"}, "AA:BB:CC:DD:EE:06", 1500},
    {"ifc-07", "10.0.0.7", "255.255.255.0", "10.0.0.255", {"fe80::7"}, "AA:BB:CC:DD:EE:07", 1500},
    {"ifc-08", "10.0.0.8", "255.255.255.0", "10.0.0.255", {"fe80::8"}, "AA:BB:CC:DD:EE:08", 1500},
    {"ifc-09", "10.0.0.9", "255.255.255.0", "10.0.0.255", {"fe80::9"}, "AA:BB:CC:DD:EE:09", 1500},
    {"ifc-10", "10.0.0.10", "255.255.255.0", "10.0.0.255", {"fe80::10"}, "AA:BB:CC:DD:EE:10", 1500}
};

class NetworkMonitoringClient {
private:
    std::unique_ptr<NetworkMonitoring::Stub> stub_;
    std::string node_id_;
    int node_idx_;

    bool PopulateNodeMetrics(NodeMetrics* metrics) {
        metrics->set_node_id(node_id_);

        // Use template for parameters
        const NodeTemplate& tpl = node_templates[node_idx_];
        metrics->set_interface_name(tpl.interface_name);
        metrics->set_mtu(tpl.mtu);
        metrics->set_ipv4(tpl.ipv4);
        metrics->set_netmask(tpl.netmask);
        metrics->set_broadcast(tpl.broadcast);
        metrics->set_mac(tpl.mac);
        for (const auto& ip6 : tpl.ipv6) metrics->add_ipv6(ip6);

        // Flags (example)
        metrics->add_flags("UP");
        metrics->add_flags("BROADCAST");
        metrics->add_flags("RUNNING");
        metrics->add_flags("MULTICAST");

        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        metrics->mutable_timestamp()->set_seconds(seconds.count());

        // Simulate stats (unique per node)
        metrics->set_rx_packets(10000 * (node_idx_ + 1));
        metrics->set_rx_bytes(1000000 * (node_idx_ + 1));
        metrics->set_tx_packets(5000 * (node_idx_ + 1));
        metrics->set_tx_bytes(500000 * (node_idx_ + 1));
        metrics->set_rx_errors(0);
        metrics->set_tx_errors(0);

        return true;
    }

public:
    NetworkMonitoringClient(std::shared_ptr<Channel> channel, const std::string& node_id, int node_idx)
        : stub_(NetworkMonitoring::NewStub(channel)), node_id_(node_id), node_idx_(node_idx) {}

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
                PopulateNodeMetrics(&metrics);

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
    std::string node_id;
    int node_idx = 0;

    // Node name template: node-01 to node-10
    if (argc > 1) {
        int node_num = std::stoi(argv[1]);
        if (node_num < 1 || node_num > 10) {
            std::cerr << "Node number must be between 1 and 10." << std::endl;
            return 1;
        }
        node_idx = node_num - 1;
        node_id = std::string("node-") + (node_num < 10 ? "0" : "") + std::to_string(node_num);
    } else {
        node_id = "node-01";
        node_idx = 0;
    }

    auto channel = grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials());
    NetworkMonitoringClient client(channel, node_id, node_idx);

    std::cout << "=== gRPC Network Monitoring Client ===" << std::endl;
    std::cout << "Node ID: " << node_id << std::endl;
    std::cout << "Server: " << target_str << std::endl;

    // Stream node metrics every 5 seconds for 5 minutes
    client.StreamNodeMetrics(5, 300);

    return 0;
}