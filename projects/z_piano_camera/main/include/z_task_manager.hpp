#pragma once
#include <thread>
#include <atomic>
#include <optional>
#include "EduScheduleManager.hpp"

namespace z {
    class TaskManager {
        public:
        TaskManager();
        ~TaskManager();

        void start();
        void stop();
        void printStatus();
    private:
        void run();

        std::thread thread_;
        std::atomic<bool> running_{false};

        // 保险用的预计停止时间（课程结束+10s）
        std::optional<std::chrono::system_clock::time_point> expectedStopTime_;

        // 当前正在录制的课程
        std::optional<edu::Occurrence> currentOcc_;

        // 上一次打印“预计结束录制时间”的时刻
        std::chrono::system_clock::time_point lastEndHintTime_{};
    };
}
