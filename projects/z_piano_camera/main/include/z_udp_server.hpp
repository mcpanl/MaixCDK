#pragma once
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace z {

    class UdpServer {
    public:
        UdpServer(const std::string& ip = "239.255.0.2", int port = 5006);
        ~UdpServer();

        // 启动 UDP 广播线程
        void start();

        // 停止 UDP 广播线程
        void stop();

        // 广播数据
        void broadcast(const std::vector<uint8_t>& data);

    private:
        std::string udp_ip;
        int udp_port;
        std::atomic<bool> running;
        std::thread udp_thread;

        std::mutex send_mutex;
        std::vector<uint8_t> pending_data;

        void run();
    };

} // namespace z