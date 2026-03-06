#pragma once
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <functional>
#include <netinet/in.h>

namespace z {

    // ======== 无锁环形队列 ========
    class LockFreeQueue {
    public:
        explicit LockFreeQueue(size_t capacity)
            : capacity_(capacity), head_(0), tail_(0) {
            buffer_.resize(capacity);
        }

        inline bool push(const std::vector<uint8_t>& data) {
            size_t head = head_.load(std::memory_order_relaxed);
            size_t next = (head + 1) % capacity_;
            if (next == tail_.load(std::memory_order_acquire)) {
                // 队列已满，丢弃最旧的
                tail_.store((tail_.load(std::memory_order_relaxed) + 1) % capacity_, std::memory_order_release);
            }
            buffer_[head] = data;
            head_.store(next, std::memory_order_release);
            return true;
        }

        inline bool pop(std::vector<uint8_t>& out) {
            size_t tail = tail_.load(std::memory_order_relaxed);
            if (tail == head_.load(std::memory_order_acquire)) {
                return false; // 空
            }
            out = std::move(buffer_[tail]);
            tail_.store((tail + 1) % capacity_, std::memory_order_release);
            return true;
        }

    private:
        std::vector<std::vector<uint8_t>> buffer_;
        size_t capacity_;
        std::atomic<size_t> head_;
        std::atomic<size_t> tail_;
    };

    // ======== 依赖注入上下文（解耦 priv/project 私有头文件）========
    //
    // 调用方（main.cpp）在创建 UdpServer 后，通过 set_context() 注入：
    //   - is_connected  : 网络是否已连通
    //   - get_ip        : 本机 IP 二进制（in_addr_t）
    //   - get_device_key: 8 字节设备 Key
    //
    // 这样 z_udp_server.cpp 不再依赖 priv.hpp，可单独编译为独立 .so。
    struct UdpContext {
        // 返回 true 表示 LAN 已连通（替代 priv.network->get_lan_state()）
        std::function<bool()>                   is_connected;
        // 返回本机 IP（替代 priv.network->get_ip_addr_binary()）
        std::function<in_addr_t()>              get_ip;
        // 返回 8 字节设备 Key（替代 priv.display->get_device_key_binary()）
        std::function<std::vector<uint8_t>()>   get_device_key;
    };

    // ======== UDP Server ========
    class UdpServer {
    public:
        UdpServer(const std::string& ip = "239.255.0.2", int port = 5006);
        ~UdpServer();

        // 注入运行时依赖（建议在 start() 之前调用，线程安全）
        void set_context(const UdpContext& ctx);

        // 启动 UDP 广播线程
        void start();

        // 停止 UDP 广播线程
        void stop();

        // 广播数据（非阻塞）
        void broadcast(const std::vector<uint8_t>& data);

    private:

        std::string udp_ip;
        int udp_port;
        std::atomic<bool> running;
        std::thread udp_thread;

        LockFreeQueue send_queue;

        UdpContext ctx_;        // 注入的依赖
        std::mutex ctx_mutex_;  // 保护 ctx_ 的读写

        void run();

        void network_monitor();   // 检测网络线程

        std::thread network_monitor_thread;
        std::atomic<bool> stop_flag {false};
        std::mutex server_mutex;
    };

} // namespace z
