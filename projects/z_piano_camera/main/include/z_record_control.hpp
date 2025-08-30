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

struct AVPacketData {
    std::vector<uint8_t> data;
    uint64_t pts = 0;
    bool is_audio = false;

    AVPacketData() = default;
    // 显式声明移动构造/移动赋值，以保证可移动
    AVPacketData(AVPacketData&&) noexcept = default;
    AVPacketData& operator=(AVPacketData&&) noexcept = default;

    // 禁止隐式拷贝（可选），避免意外拷贝开销
    AVPacketData(const AVPacketData&) = delete;
    AVPacketData& operator=(const AVPacketData&) = delete;
};

namespace z {

    class RecordControl {
    public:
        enum class State {
            WaitingSpsPps,   // 等待 SPS/PPS
            Ready,           // 已就绪，未开始录制
            Starting,        // 正在开始录制
            Recording,       // 正在录制
            Stopping         // 正在停止录制
        };

        RecordControl();
        ~RecordControl();

        // 设置录制文件名
        void setFileName(const std::string& filename);

        // 开始录制（状态切换）
        void start();

        // 停止录制
        void stop();

        // 获取录制状态
        State state() const;

        // 获取当前录制时长（秒）
        double duration() const;

        // 获取当前文件名
        std::string fileName() const;

        void handleVideoFrame(uint8_t* data, int size,
                              const std::vector<uint8_t>& sps_pps,
                              uint64_t now_ms);


        void handleAudioFrame(int sample_rate,
                              int bytes_per_sample,
                              uint64_t now_ms,
                              uint64_t& last_audio_ms,
                              double& sample_error_acc);
    private:
        State m_state;
        std::string m_filename;
        std::chrono::steady_clock::time_point m_startTime;
        std::chrono::steady_clock::time_point m_stopTime;
        std::vector<uint8_t> cached_sps_pps;
        bool sps_pps_ready = false;
        void resetTimer();

        void pushThreadLoop();
        void enqueueFrame(const uint8_t* data, int size, uint64_t pts, bool is_audio);

        // 线程/同步/队列
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::condition_variable m_stop_cv; // stop() 等待 push 线程完成
        std::thread m_pushThread;
        std::atomic<bool> m_running{false};
        std::queue<std::unique_ptr<AVPacketData>> m_avQueue;


        // 可配置的队列上限（防止无限膨胀）
        size_t m_max_queue_size = 200; // 根据实际场景调整
        size_t m_dropped_packets = 0;   // 统计被丢弃的包数（可选）

        bool m_packer_opened = false;
        bool m_packer_closed = false;
    };

} // namespace z
