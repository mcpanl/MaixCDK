#include "z_udp_server.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <fcntl.h>        // ✅ fcntl, O_NONBLOCK
#include <sys/select.h>   // ✅ select, fd_set, FD_ZERO, FD_SET, FD_ISSET
#include <sys/time.h>     // ✅ struct timeval
#include "priv.hpp"

namespace z {

UdpServer::UdpServer(const std::string& ip, int port)
    : udp_ip(ip), udp_port(port), running(false), stop_flag(false),
      send_queue(128) // 环形队列容量
{
    printf("==== UdpServer ====\n");

    network_monitor_thread = std::thread([this]() { network_monitor(); });
}

UdpServer::~UdpServer() {
    printf("~~~~ UdpServer ~~~~\n");

    stop_flag = true;
    if (network_monitor_thread.joinable()) {
        network_monitor_thread.join();
    }

    stop();
}

void UdpServer::start() {
    std::lock_guard<std::mutex> lock(server_mutex);
    if (running) return;

    running = true;
    udp_thread = std::thread([this]() { run(); });
    std::cout << "[UDP] Server started on " << udp_ip << ":" << udp_port << "\n";
}

void UdpServer::stop() {
    std::lock_guard<std::mutex> lock(server_mutex);
    if (!running) return;

    running = false;
    if (udp_thread.joinable()) {
        udp_thread.join();
    }
    std::cout << "[UDP] Server stopped.\n";
}

// ✅ 无锁广播，不会阻塞
void UdpServer::broadcast(const std::vector<uint8_t>& data) {
    send_queue.push(data);
}

void UdpServer::run() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return;
    }

    // 设置非阻塞
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

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

    const size_t MAX_PACKET_SIZE = 1300;
    const size_t HEADER_SIZE = 24;
    const size_t CHUNK_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;
    uint32_t frame_id = 0;

    printf("UDP 启动完毕 (非阻塞模式)\n");

    while (running) {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(sock, &writefds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 5000; // 最多等待 5ms

        int ret = select(sock + 1, nullptr, &writefds, nullptr, &tv);
        if (ret <= 0) {
            continue; // 超时或错误，继续 loop
        }

        if (FD_ISSET(sock, &writefds)) {
            std::vector<uint8_t> data_copy;
            if (!send_queue.pop(data_copy)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue; // 没有数据要发
            }

            // ssize_t sent = sendto(sock, data_copy.data(), data_copy.size(), 0,
            //                       (struct sockaddr*)&addr, sizeof(addr));
            // if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            //     perror("UDP sendto failed");
            // }

            frame_id++;
            size_t total_packets = (data_copy.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
            in_addr_t local_ip_binary = INADDR_ANY;
            if (priv.network) {
                local_ip_binary = priv.network->get_ip_addr_binary();
            }

            for (size_t packet_id = 0; packet_id < total_packets; ++packet_id) {
                size_t start = packet_id * CHUNK_SIZE;
                size_t end = std::min(start + CHUNK_SIZE, data_copy.size());
                size_t payload_size = end - start;

                uint8_t buffer[MAX_PACKET_SIZE] = {0};

                // UDP Header: frame_id (4) | total_packets (2) | packet_id (2) | ip_addr (4) | device_key (8) | status (4)
                buffer[0] = (frame_id >> 24) & 0xFF;
                buffer[1] = (frame_id >> 16) & 0xFF;
                buffer[2] = (frame_id >> 8) & 0xFF;
                buffer[3] = frame_id & 0xFF;
                buffer[4] = (total_packets >> 8) & 0xFF;
                buffer[5] = total_packets & 0xFF;
                buffer[6] = (packet_id >> 8) & 0xFF;
                buffer[7] = packet_id & 0xFF;

                memcpy(buffer + 8, &local_ip_binary, 4);

                if (priv.display) {
                    auto device_binary = priv.display->get_device_key_binary();

                    if (device_binary.size() > 0) {
                        memcpy(buffer + 12, device_binary.data(), 8);
                    }
                }

                // status (4) -> 先填 0
                uint32_t status = 0;
                memcpy(buffer + 20, &status, 4);

                memcpy(buffer + HEADER_SIZE, data_copy.data() + start, payload_size);

                ssize_t sent = sendto(sock, buffer, HEADER_SIZE + payload_size, 0,
                                      (struct sockaddr*)&addr, sizeof(addr));
                if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("UDP sendto failed");
                }
            }
        }
    }

    close(sock);
}

void UdpServer::network_monitor() {
    LanState last_state = LanState::DISCONNECTED;
    int stable_count = 0;

    while (!stop_flag) {
        if (priv.network) {
            LanState state = priv.network->get_lan_state();

            if (state == LanState::CONNECTED) {
                stable_count++;
                if (!running && stable_count >= 1) {  // 连续 1 次(约0.5秒)才启动
                    std::cout << "[NET][UDP] Network stable, starting UDP server...\n";
                    start();
                }
            } else {
                if (last_state == LanState::CONNECTED && running) {
                    std::cout << "[NET][UDP] Network lost, stopping UDP server...\n";
                    stop();
                }
                stable_count = 0;
            }

            last_state = state;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace z
