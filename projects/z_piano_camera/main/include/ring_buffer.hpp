#pragma once
#include <string>
#include <chrono>
#include <vector>
#include <thread>              // std::thread
#include <mutex>               // std::mutex, std::lock_guard, std::unique_lock
#include <condition_variable>  // std::condition_variable
#include <queue>               // std::queue
#include <atomic>              // std::atomic
#include <vector>              // std::vector (用于缓存 data)
#include <cstdint>             // uint8_t, uint64_t
#include <iostream>            // std::cout, std::flush
#include <vector>
#include <mutex>
#include <cstring>
#include <vector>
#include <mutex>
#include <stdexcept>


template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buffer_(capacity), capacity_(capacity), head_(0), tail_(0), size_(0) {}

    // 非阻塞写（不会丢弃旧数据），返回实际写入字节数
    size_t write(const T* data, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t to_write = std::min(len, writable_bytes_nolock());
        size_t written = 0;
        for (size_t i = 0; i < to_write; ++i) {
            buffer_[head_] = data[i];
            head_ = (head_ + 1) % capacity_;
            ++size_;
            ++written;
        }
        if (written > 0) cv_.notify_one();
        return written;
    }

    // 强制写：若空间不足会丢弃最旧的数据以腾出空间，保证尽量写入全部 len（如果 len > capacity，则只写入最后 capacity 字节）
    size_t write_force(const T* data, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (len >= capacity_) {
            // 只写入最后 capacity_ 字节
            const T* src = data + (len - capacity_);
            // 重置为空并写入
            head_ = tail_ = size_ = 0;
            for (size_t i = 0; i < capacity_; ++i) {
                buffer_[head_] = src[i];
                head_ = (head_ + 1) % capacity_;
                ++size_;
            }
            cv_.notify_one();
            return capacity_;
        }

        // 如果空间不足，丢弃最旧的数据
        size_t need_space = len;
        if (need_space > writable_bytes_nolock()) {
            size_t to_drop = need_space - writable_bytes_nolock();
            drop_oldest_nolock(to_drop);
        }

        // 写入
        size_t written = 0;
        for (size_t i = 0; i < len; ++i) {
            buffer_[head_] = data[i];
            head_ = (head_ + 1) % capacity_;
            ++size_;
            ++written;
        }
        cv_.notify_one();
        return written;
    }

    // 非阻塞读（消费数据），返回实际读取字节数
    size_t read(T* out, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t to_read = std::min(len, readable_bytes_nolock());
        size_t r = 0;
        for (size_t i = 0; i < to_read; ++i) {
            out[i] = buffer_[tail_];
            tail_ = (tail_ + 1) % capacity_;
            --size_;
            ++r;
        }
        return r;
    }

    // 等待直到读到指定字节数，或超时（max_wait_us 毫秒单位），返回实际读取字节数
    // 若 max_wait_us <= 0 表示不等待（即时返回）
    size_t read_exact_wait(T* out, size_t len, int64_t max_wait_us) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (len == 0) return 0;

        if (max_wait_us <= 0) {
            // 立即读
            size_t to_read = std::min(len, readable_bytes_nolock());
            size_t r = 0;
            for (size_t i = 0; i < to_read; ++i) {
                out[i] = buffer_[tail_];
                tail_ = (tail_ + 1) % capacity_;
                --size_;
                ++r;
            }
            return r;
        }

        auto deadline = std::chrono::microseconds(max_wait_us);
        auto start = std::chrono::steady_clock::now();
        size_t needed = len;
        size_t total_read = 0;
        while (total_read < len) {
            // 如果已有足够的数据，直接读取剩余
            size_t avail = readable_bytes_nolock();
            if (avail > 0) {
                size_t take = std::min(avail, needed);
                for (size_t i = 0; i < take; ++i) {
                    out[total_read + i] = buffer_[tail_];
                    tail_ = (tail_ + 1) % capacity_;
                }
                size_ -= take;
                total_read += take;
                needed -= take;
                if (total_read >= len) break;
            }

            // 等待剩余时间或被 notify
            auto elapsed = std::chrono::steady_clock::now() - start;
            auto remain = (elapsed >= deadline) ? std::chrono::microseconds(0) : (deadline - elapsed);
            if (remain.count() == 0) break;
            cv_.wait_for(lock, remain);
        }
        return total_read;
    }

    // 查看（不消费）最多 len 字节，返回实际 peek 的字节数
    size_t peek(T* out, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t to_peek = std::min(len, readable_bytes_nolock());
        size_t idx = tail_;
        for (size_t i = 0; i < to_peek; ++i) {
            out[i] = buffer_[idx];
            idx = (idx + 1) % capacity_;
        }
        return to_peek;
    }

    // 丢弃最旧的数据（返回实际丢弃字节数）
    size_t drop_oldest(size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        return drop_oldest_nolock(len);
    }

    // 返回当前可读字节数（线程安全）
    size_t readable_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return readable_bytes_nolock();
    }

    // 返回可写字节数（线程安全）
    size_t writable_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return writable_bytes_nolock();
    }

    // 是否能写入 len 字节（线程安全）
    bool can_write(size_t len) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return writable_bytes_nolock() >= len;
    }

    // 清空缓冲区
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = tail_ = size_ = 0;
    }

    // 当前占用（线程安全）
    size_t size() const {
        return readable_bytes();
    }

    // 容量（线程安全）
    size_t capacity() const {
        return capacity_;
    }

    bool is_empty() const {
        return size() == 0;
    }

    bool is_full() const {
        return size() == capacity_;
    }

private:
    // nolock 版本，内部使用
    size_t readable_bytes_nolock() const {
        return size_;
    }
    size_t writable_bytes_nolock() const {
        return capacity_ - size_;
    }
    // nolock 丢弃实现
    size_t drop_oldest_nolock(size_t len) {
        size_t to_drop = std::min(len, size_);
        if (to_drop == 0) return 0;
        // 直接移动 tail 指针
        tail_ = (tail_ + to_drop) % capacity_;
        size_ -= to_drop;
        return to_drop;
    }

private:
    std::vector<T> buffer_;
    size_t capacity_;
    size_t head_;   // 写指针（下一个写入位置）
    size_t tail_;   // 读指针（下一个读取位置）
    size_t size_;   // 当前数据量（字节）
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};