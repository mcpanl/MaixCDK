/**
 * z_udp_server.cpp —— UDP 图传广播实现
 *
 * ⚠️  本文件属于「闭源」部分，不随项目源码发布。
 *      分发时仅提供预编译的 prebuilt/libz_udp_server.so。
 *
 * 依赖注入：
 *   通过 UdpServer::set_context() 传入三个 std::function 回调，
 *   彻底解耦 priv.hpp / z_network.hpp / z_display.hpp 等项目私有头文件，
 *   使本文件可独立编译为 .so，无需项目业务模块的头文件。
 */
#include "z_udp_server.hpp"

#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <cerrno>
#include <cstdio>

namespace z {

UdpServer::UdpServer(const std::string& ip, int port)
    : udp_ip(ip), udp_port(port), running(false), stop_flag(false),
      send_queue(128)
{
    printf("==== UdpServer ====\n");

    // 启动网络监控线程（ctx_ 未注入时会跳过检测，等待 set_context 后生效）
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

void UdpServer::set_context(const UdpContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    ctx_ = ctx;
}

void UdpServer::start() {
    std::lock_guard<std::mutex> lock(server_mutex);
    if (running) return;

    running = true;
    udp_thread = std::thread([this]() { run(); });
    printf("[UDP] Server started on %s:%d\n", udp_ip.c_str(), udp_port);
}

void UdpServer::stop() {
    std::lock_guard<std::mutex> lock(server_mutex);
    if (!running) return;

    running = false;
    if (udp_thread.joinable()) {
        udp_thread.join();
    }
    printf("[UDP] Server stopped.\n");
}

// 无锁广播，不会阻塞调用方
void UdpServer::broadcast(const std::vector<uint8_t>& data) {
    send_queue.push(data);
}

void UdpServer::run() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return;
    }

    // 设置非阻塞模式
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int broadcastEnable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt SO_BROADCAST failed");
        close(sock);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(udp_port);
    addr.sin_addr.s_addr = inet_addr(udp_ip.c_str());

    // 协议常量
    const size_t MAX_PACKET_SIZE = 1300;
    const size_t HEADER_SIZE     = 24;
    const size_t CHUNK_SIZE      = MAX_PACKET_SIZE - HEADER_SIZE;
    uint32_t frame_id = 0;

    printf("[UDP] 启动完毕 (非阻塞模式), 目标: %s:%d\n", udp_ip.c_str(), udp_port);

    while (running) {
        // 用 select 等待 socket 可写，避免忙轮询
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(sock, &writefds);

        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 5000; // 最多等 5ms

        int ret = select(sock + 1, nullptr, &writefds, nullptr, &tv);
        if (ret <= 0) {
            continue; // 超时或错误，继续循环
        }

        if (!FD_ISSET(sock, &writefds)) {
            continue;
        }

        // 从队列取一帧
        std::vector<uint8_t> data_copy;
        if (!send_queue.pop(data_copy)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        frame_id++;

        // 通过回调获取 IP（线程安全读取 ctx_）
        in_addr_t local_ip_binary = INADDR_ANY;
        {
            std::lock_guard<std::mutex> lock(ctx_mutex_);
            if (ctx_.get_ip) {
                local_ip_binary = ctx_.get_ip();
            }
        }

        // 通过回调获取设备 Key（一次性取出，减少锁竞争）
        std::vector<uint8_t> device_key_buf;
        {
            std::lock_guard<std::mutex> lock(ctx_mutex_);
            if (ctx_.get_device_key) {
                device_key_buf = ctx_.get_device_key();
            }
        }

        size_t total_packets = (data_copy.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;

        for (size_t packet_id = 0; packet_id < total_packets; ++packet_id) {
            size_t start_off  = packet_id * CHUNK_SIZE;
            size_t end_off    = std::min(start_off + CHUNK_SIZE, data_copy.size());
            size_t payload_sz = end_off - start_off;

            uint8_t buffer[MAX_PACKET_SIZE] = {0};

            // UDP Header（24 字节）:
            //   [0..3]  frame_id      (uint32 big-endian)
            //   [4..5]  total_packets (uint16 big-endian)
            //   [6..7]  packet_id     (uint16 big-endian)
            //   [8..11] ip_addr       (in_addr_t, native)
            //   [12..19] device_key  (8 bytes)
            //   [20..23] status      (uint32, 0)
            buffer[0] = (frame_id >> 24) & 0xFF;
            buffer[1] = (frame_id >> 16) & 0xFF;
            buffer[2] = (frame_id >>  8) & 0xFF;
            buffer[3] =  frame_id        & 0xFF;

            buffer[4] = (total_packets >> 8) & 0xFF;
            buffer[5] =  total_packets       & 0xFF;

            buffer[6] = (packet_id >> 8) & 0xFF;
            buffer[7] =  packet_id       & 0xFF;

            memcpy(buffer + 8, &local_ip_binary, 4);

            if (device_key_buf.size() >= 8) {
                memcpy(buffer + 12, device_key_buf.data(), 8);
            }

            // status 固定 0
            uint32_t status = 0;
            memcpy(buffer + 20, &status, 4);

            memcpy(buffer + HEADER_SIZE, data_copy.data() + start_off, payload_sz);

            ssize_t sent = sendto(sock, buffer, HEADER_SIZE + payload_sz, 0,
                                  (struct sockaddr*)&addr, sizeof(addr));
            if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[UDP] sendto failed");
            }
        }
    }

    close(sock);
}

void UdpServer::network_monitor() {
    bool last_connected = false;
    int  stable_count   = 0;

    while (!stop_flag) {
        // 线程安全地读取回调
        bool connected = false;
        {
            std::lock_guard<std::mutex> lock(ctx_mutex_);
            if (ctx_.is_connected) {
                connected = ctx_.is_connected();
            }
        }

        if (connected) {
            stable_count++;
            if (!running && stable_count >= 1) {
                printf("[NET][UDP] 网络稳定，启动 UDP server...\n");
                start();
            }
        } else {
            if (last_connected && running) {
                printf("[NET][UDP] 网络断开，停止 UDP server...\n");
                stop();
            }
            stable_count = 0;
        }

        last_connected = connected;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace z
