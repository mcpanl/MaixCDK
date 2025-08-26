#include "z_udp_server.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace z {

UdpServer::UdpServer(const std::string& ip, int port)
    : udp_ip(ip), udp_port(port), running(false) {}

UdpServer::~UdpServer() {
    stop();
}

void UdpServer::start() {
    running = true;
    udp_thread = std::thread([this]() { run(); });
}

void UdpServer::stop() {
    running = false;
    if (udp_thread.joinable()) {
        udp_thread.join();
    }
}

void UdpServer::broadcast(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(send_mutex);
    pending_data = data; // 简单保存最新一帧数据
}

void UdpServer::run() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return;
    }

    int broadcastEnable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt failed");
        close(sock);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(udp_port);
    addr.sin_addr.s_addr = inet_addr(udp_ip.c_str());

    const size_t MAX_PACKET_SIZE = 1400;
    const size_t HEADER_SIZE = 12;
    const size_t CHUNK_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;
    uint32_t frame_id = 0;

    while (running) {
        std::vector<uint8_t> data_copy;
        {
            std::lock_guard<std::mutex> lock(send_mutex);
            if (pending_data.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            data_copy = pending_data;
            pending_data.clear();
        }

        frame_id++;
        size_t total_packets = (data_copy.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;

        for (size_t packet_id = 0; packet_id < total_packets; ++packet_id) {
            size_t start = packet_id * CHUNK_SIZE;
            size_t end = std::min(start + CHUNK_SIZE, data_copy.size());
            size_t payload_size = end - start;

            uint8_t buffer[MAX_PACKET_SIZE] = {0};

            // UDP Header: frame_id (4) | total_packets (2) | packet_id (2) | reserved (4)
            buffer[0] = (frame_id >> 24) & 0xFF;
            buffer[1] = (frame_id >> 16) & 0xFF;
            buffer[2] = (frame_id >> 8) & 0xFF;
            buffer[3] = frame_id & 0xFF;
            buffer[4] = (total_packets >> 8) & 0xFF;
            buffer[5] = total_packets & 0xFF;
            buffer[6] = (packet_id >> 8) & 0xFF;
            buffer[7] = packet_id & 0xFF;

            memcpy(buffer + HEADER_SIZE, data_copy.data() + start, payload_size);

            ssize_t sent = sendto(sock, buffer, HEADER_SIZE + payload_size, 0,
                                  (struct sockaddr*)&addr, sizeof(addr));
            if (sent < 0) {
                perror("UDP sendto failed");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    close(sock);
}

} // namespace z