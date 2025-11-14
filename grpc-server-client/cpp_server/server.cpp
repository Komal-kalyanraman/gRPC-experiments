#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "network.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;
using network::WifiMetrics;
using network::MetricsAck;
using network::NetworkMonitoring;

class NetworkMonitoringServiceImpl final : public NetworkMonitoring::Service {
private:
    struct NodeConnection {
        std::string node_id;
        std::chrono::system_clock::time_point last_seen;
    };

    std::mutex metrics_mutex_;
    mutable std::mutex nodes_mutex_;  // <-- Add 'mutable' here
    std::map<std::string, NodeConnection> active_nodes_;
    std::vector<WifiMetrics> metrics_history_;
    static const size_t MAX_METRICS_HISTORY = 1000;

    void LogMetrics(const WifiMetrics& metrics) {
        std::cout << "\n=== Wi-Fi Metrics from Node: " << metrics.node_id() << " ===" << std::endl;
        std::cout << "Interface: " << metrics.interface_name() << std::endl;
        std::cout << "Timestamp: " << metrics.timestamp().seconds() << std::endl;
        
        std::cout << "Flags: ";
        for (const auto& flag : metrics.flags()) {
            std::cout << flag << " ";
        }
        std::cout << std::endl;
        
        std::cout << "MTU: " << metrics.mtu() << std::endl;
        std::cout << "IPv4: " << metrics.ipv4() << std::endl;
        std::cout << "Netmask: " << metrics.netmask() << std::endl;
        std::cout << "Broadcast: " << metrics.broadcast() << std::endl;
        std::cout << "MAC: " << metrics.mac() << std::endl;
        
        std::cout << "IPv6 addresses: ";
        for (const auto& ipv6 : metrics.ipv6()) {
            std::cout << ipv6 << " ";
        }
        std::cout << std::endl;
        
        std::cout << "RX: " << metrics.rx_packets() << " packets, " 
                  << metrics.rx_bytes() << " bytes, "
                  << metrics.rx_errors() << " errors" << std::endl;
        std::cout << "TX: " << metrics.tx_packets() << " packets, " 
                  << metrics.tx_bytes() << " bytes, "
                  << metrics.tx_errors() << " errors" << std::endl;
    }

    void DetectAnomalies(const WifiMetrics& metrics) {
        if (metrics.rx_errors() > 0 || metrics.tx_errors() > 0) {
            std::cerr << "\n⚠️  ALERT: Node " << metrics.node_id() 
                      << " interface " << metrics.interface_name()
                      << " has errors - RX: " << metrics.rx_errors()
                      << ", TX: " << metrics.tx_errors() << std::endl;
        }
    }

public:
    Status StreamWifiMetrics(ServerContext* context,
                            ServerReaderWriter<MetricsAck, WifiMetrics>* stream) override {
        WifiMetrics metrics;
        std::string node_id;

        std::cout << "\n[StreamWifiMetrics] New streaming connection established" << std::endl;

        while (stream->Read(&metrics)) {
            node_id = metrics.node_id();

            {
                std::lock_guard<std::mutex> lock(nodes_mutex_);
                active_nodes_[node_id] = {
                    node_id,
                    std::chrono::system_clock::now()
                };
            }

            // Store metrics
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                metrics_history_.push_back(metrics);
                if (metrics_history_.size() > MAX_METRICS_HISTORY) {
                    metrics_history_.erase(metrics_history_.begin());
                }
            }

            // Log and analyze metrics
            LogMetrics(metrics);
            DetectAnomalies(metrics);

            // Send acknowledgment
            MetricsAck ack;
            ack.set_node_id(node_id);
            ack.set_success(true);
            
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
            
            ack.mutable_server_timestamp()->set_seconds(seconds.count());
            ack.set_message("Wi-Fi metrics received and stored");

            if (!stream->Write(ack)) {
                std::cerr << "Failed to send acknowledgment to node " << node_id << std::endl;
                return Status(grpc::StatusCode::INTERNAL, "Failed to send ack");
            }

            std::cout << "[StreamWifiMetrics] Acknowledgment sent to " << node_id << std::endl;
        }

        std::cout << "[StreamWifiMetrics] Connection closed for node " << node_id << std::endl;
        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            active_nodes_.erase(node_id);
        }

        return Status::OK;
    }

    void PrintActiveNodes() const {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        std::cout << "\n=== Active Nodes ===" << std::endl;
        for (const auto& entry : active_nodes_) {
            auto duration = std::chrono::system_clock::now() - entry.second.last_seen;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
            std::cout << "  - " << entry.first << " (last seen " << seconds << "s ago)" << std::endl;
        }
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    NetworkMonitoringServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    // Set max message size to 4MB
    builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
    builder.SetMaxSendMessageSize(4 * 1024 * 1024);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Network Monitoring Server listening on " << server_address << std::endl;
    std::cout << "Waiting for Wi-Fi metric streams from nodes..." << std::endl;

    // Periodically print active nodes
    std::thread status_thread([&service]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            service.PrintActiveNodes();
        }
    });
    status_thread.detach();

    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}