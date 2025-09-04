#pragma once
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <string>

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

    // ======== UDP Server ========
    class UdpServer {
    public:
        UdpServer(const std::string& ip = "239.255.0.2", int port = 5006);
        ~UdpServer();

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

        LockFreeQueue send_queue;  // ✅ 替换掉 std::deque + mutex

        void run();
    };

} // namespace z
