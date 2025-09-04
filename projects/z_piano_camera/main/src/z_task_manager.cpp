#include "z_task_manager.hpp"
#include <chrono>
#include <thread>
#include <algorithm>
#include "maix_basic.hpp"
#include "priv.hpp"

using namespace edu;
using namespace maix;
using namespace std::chrono;

namespace z {
    // 确保目录存在
    static void ensure_dir(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            mkdir(path.c_str(), 0755);
        }
    }

    // 获取当前时间戳字符串
    static std::string timestamp_str() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return oss.str();
    }

    TaskManager::TaskManager() {
        printf("==== TaskManager ====\n");
        start();
    }
    TaskManager::~TaskManager() {
        stop();
    }

    void TaskManager::start() {
        if (running_) return;
        running_ = true;
        thread_ = std::thread(&TaskManager::run, this);
    }

    void TaskManager::stop() {
        if (!running_) return;
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void TaskManager::printStatus() {
        log::info("TaskManager is %s", running_ ? "running" : "stopped");
    }

    void TaskManager::run() {
        using Clock = std::chrono::system_clock;
        TimePoint lastHintTime{};

        while (running_) {
            auto now = Clock::now();

            // 1. 最近1小时要开始的课程
            auto upcoming = priv.manager->occurrencesStartingWithin1h(now);
            if (!upcoming.empty()) {
                auto nearest = *std::min_element(
                    upcoming.begin(), upcoming.end(),
                    [](const Occurrence& a, const Occurrence& b) {
                        return a.start < b.start;
                    });

                auto ms_to_start = duration_cast<milliseconds>(nearest.start - now).count();

                // 倒计时（30分钟内，每分钟打印一次）
                if (ms_to_start > 0 && ms_to_start <= 30 * 60 * 1000 &&
                    (!priv.recordControl || priv.recordControl->state() == RecordControl::State::Ready)) {

                    if (lastHintTime.time_since_epoch().count() == 0 ||
                        duration_cast<seconds>(now - lastHintTime).count() >= 60) {

                        int seconds_to_start = ms_to_start / 1000 - 15;
                        log::info("预计 %d 秒后开始录制 (课程 %s)",
                                  seconds_to_start, nearest.scheduleId.c_str());
                        lastHintTime = now;
                    }
                }

                // 开始前15秒 -> 启动录制
                if (ms_to_start <= 15 * 1000 &&
                    (!priv.recordControl || priv.recordControl->state() == RecordControl::State::Ready)) {

                    ensure_dir("/root/record");
                    std::string filename = "/root/record/" + timestamp_str() + ".mp4";
                    priv.recordControl->setFileName(filename);
                    priv.recordControl->start();
                    // 保存当前课程
                    currentOcc_ = nearest;
                    // 记录预计的停止时间（课程结束时间 + 10s）
                    expectedStopTime_ = nearest.end + seconds(10);

                    log::info("课程即将开始，提前15秒启动录制: %s", filename.c_str());
                    }
            }

            // 如果正在录制，并且有当前课程
            if (priv.recordControl &&
                priv.recordControl->state() == RecordControl::State::Recording &&
                currentOcc_) {

                auto now = Clock::now();
                auto ms_to_end = duration_cast<milliseconds>(currentOcc_->end - now).count();

                if (ms_to_end > 0 && ms_to_end <= 30 * 60 * 1000) {
                    // 每分钟打印一次
                    if (lastEndHintTime_.time_since_epoch().count() == 0 ||
                        duration_cast<seconds>(now - lastEndHintTime_).count() >= 60) {

                        time_t end_ts = Clock::to_time_t(currentOcc_->end);
                        char buf[64];
                        strftime(buf, sizeof(buf), "%F %T", std::localtime(&end_ts));

                        log::info("当前课程预计结束时间: %s (课程 %s)",
                                  buf, currentOcc_->scheduleId.c_str());

                        lastEndHintTime_ = now;
                        }
                }
                }


            // ====== 停止录制 ======
            auto status = priv.manager->statusAt(now);


            // 如果课程已结束，直接停止
            if (!status.busy &&
                priv.recordControl &&
                priv.recordControl->state() == RecordControl::State::Recording) {

                double elapsed = priv.recordControl->duration();
                log::info("课程已结束，准备停止录制，持续: %.2f 秒", elapsed);
                priv.recordControl->stop();
                log::info("录制结束，持续: %.2f 秒", elapsed);
                currentOcc_.reset();
                }

            // 保险机制：超过 expectedStopTime 也强制停止
            if (expectedStopTime_ && now >= *expectedStopTime_ &&
                priv.recordControl &&
                priv.recordControl->state() == RecordControl::State::Recording) {

                double elapsed = priv.recordControl->duration();
                log::warn("超过预计结束时间仍未停止，强制停止录制，持续: %.2f 秒", elapsed);
                priv.recordControl->stop();
                expectedStopTime_.reset();
                currentOcc_.reset();
            }
            std::this_thread::sleep_for(milliseconds(200));
        }
    }

} // namespace z
