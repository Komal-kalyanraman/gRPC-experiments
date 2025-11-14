#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

#include <grpcpp/grpcpp.h>
#include "network.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;
using network::NodeMetrics;
using network::MetricsAck;
using network::NetworkMonitoring;
using json = nlohmann::json;

const std::string STATUS_FILE = "../../../node_status.json";

class NetworkMonitoringServiceImpl final : public NetworkMonitoring::Service {
private:
    struct NodeConnection {
        std::string node_id;
        std::chrono::system_clock::time_point last_seen;
        std::chrono::system_clock::time_point last_disconnected;
        bool online = false;
        std::chrono::seconds total_downtime{0};
    };

    std::mutex metrics_mutex_;
    mutable std::mutex nodes_mutex_;
    std::map<std::string, NodeConnection> node_status_;
    std::vector<NodeMetrics> metrics_history_;
    static const size_t MAX_METRICS_HISTORY = 1000;

    // Initialize JSON file with default values
    void InitStatusFile() {
        json j;
        for (int i = 1; i <= 10; ++i) {
            std::string node = std::string("node-") + (i < 10 ? "0" : "") + std::to_string(i);
            j[node] = {
                {"status", "offline"},
                {"total_downtime", 0},
                {"last_seen", 0}
            };
            // Also initialize in-memory status
            NodeConnection nc;
            nc.node_id = node;
            nc.online = false;
            nc.total_downtime = std::chrono::seconds(0);
            nc.last_seen = std::chrono::system_clock::time_point();
            nc.last_disconnected = std::chrono::system_clock::time_point();
            node_status_[node] = nc;
        }
        std::ofstream ofs(STATUS_FILE);
        ofs << j.dump(4);
        ofs.close();
    }

    // Update JSON file with current node status
    void UpdateStatusFile() {
        json j;
        for (const auto& entry : node_status_) {
            const auto& node = entry.second;
            std::string status = node.online ? "online" : "offline";
            long last_seen = 0;
            if (node.online || node.last_seen.time_since_epoch().count() > 0) {
                last_seen = std::chrono::duration_cast<std::chrono::seconds>(
                    node.last_seen.time_since_epoch()).count();
            }
            long total_downtime = node.total_downtime.count();
            if (!node.online && node.last_disconnected.time_since_epoch().count() > 0) {
                auto current_down = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now() - node.last_disconnected).count();
                total_downtime += current_down;
            }
            j[node.node_id] = {
                {"status", status},
                {"total_downtime", total_downtime},
                {"last_seen", last_seen}
            };
        }
        std::ofstream ofs(STATUS_FILE);
        ofs << j.dump(4);
        ofs.close();
    }

    void LogMetrics(const NodeMetrics& metrics) {
        std::cout << "\n=== Node Metrics from Node: " << metrics.node_id() << " ===" << std::endl;
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

public:
    NetworkMonitoringServiceImpl() {
        InitStatusFile();
    }

    Status StreamNodeMetrics(ServerContext* context,
                            ServerReaderWriter<MetricsAck, NodeMetrics>* stream) override {
        NodeMetrics metrics;
        std::string node_id;

        std::cout << "\n[StreamNodeMetrics] New streaming connection established" << std::endl;

        while (stream->Read(&metrics)) {
            node_id = metrics.node_id();

            {
                std::lock_guard<std::mutex> lock(nodes_mutex_);
                auto& node = node_status_[node_id];
                node.node_id = node_id;
                node.last_seen = std::chrono::system_clock::now();
                bool was_offline = !node.online;
                node.online = true;

                // If node was previously offline, add downtime to total
                if (was_offline && node.last_disconnected.time_since_epoch().count() > 0) {
                    auto downtime = std::chrono::duration_cast<std::chrono::seconds>(
                        node.last_seen - node.last_disconnected);
                    node.total_downtime += downtime;
                    std::cout << "[Server] Node " << node_id << " was down for " << downtime.count() << " seconds." << std::endl;
                    node.last_disconnected = std::chrono::system_clock::time_point();
                }
                UpdateStatusFile();
            }

            // Store metrics
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                metrics_history_.push_back(metrics);
                if (metrics_history_.size() > MAX_METRICS_HISTORY) {
                    metrics_history_.erase(metrics_history_.begin());
                }
            }

            // Log metrics
            LogMetrics(metrics);

            // Send acknowledgment
            MetricsAck ack;
            ack.set_node_id(node_id);
            ack.set_success(true);
            
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
            
            ack.mutable_server_timestamp()->set_seconds(seconds.count());
            ack.set_message("Node metrics received and stored");

            if (!stream->Write(ack)) {
                std::cerr << "Failed to send acknowledgment to node " << node_id << std::endl;
                return Status(grpc::StatusCode::INTERNAL, "Failed to send ack");
            }

            std::cout << "[StreamNodeMetrics] Acknowledgment sent to " << node_id << std::endl;
        }

        std::cout << "🔴 [StreamNodeMetrics] Connection lost for node " << node_id << std::endl;
        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            auto& node = node_status_[node_id];
            node.online = false;
            node.last_disconnected = std::chrono::system_clock::now();
            UpdateStatusFile();
        }

        return Status::OK;
    }

    void PrintActiveNodes() const {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        std::cout << "\n=== Node Status ===" << std::endl;
        for (const auto& entry : node_status_) {
            const auto& node = entry.second;
            if (node.online) {
                auto duration = std::chrono::system_clock::now() - node.last_seen;
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
                std::cout << "  - " << node.node_id << " (last seen " << seconds << "s ago, ONLINE, total downtime: " << node.total_downtime.count() << "s)" << std::endl;
            } else if (node.last_disconnected.time_since_epoch().count() > 0) {
                auto duration = std::chrono::system_clock::now() - node.last_disconnected;
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
                std::cout << "  - " << node.node_id << " (still down, down for " << seconds << "s, total downtime: " << (node.total_downtime + std::chrono::seconds(seconds)).count() << "s)" << std::endl;
            } else {
                std::cout << "  - " << node.node_id << " (never connected)" << std::endl;
            }
        }
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    NetworkMonitoringServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
    builder.SetMaxSendMessageSize(4 * 1024 * 1024);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Network Monitoring Server listening on " << server_address << std::endl;
    std::cout << "Waiting for node metric streams from nodes..." << std::endl;

    std::thread status_thread([&service]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(20));
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