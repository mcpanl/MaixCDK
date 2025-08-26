#pragma once
#include <string>
#include <chrono>
#include <vector>

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
    };

} // namespace z
