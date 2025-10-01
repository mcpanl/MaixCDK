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
    std::string nowToYMDTHISZ(TimePoint time) {
        // 转为 time_t
        std::time_t t = Clock::to_time_t(time);

        // 转为 UTC 时间
        std::tm tm{};
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&tm, &t);   // Windows 安全版本
#else
        gmtime_r(&t, &tm);   // Linux/Unix 安全版本
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
        return oss.str();
    }

    std::string makeFileName(std::string studentId, std::string deviceId, TimePoint time) {
        return studentId + "_" + nowToYMDTHISZ(time) + "_" + deviceId;
    }

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
        TimePoint lastStartHintTime{};
        TimePoint lastEndHintTime{};

        // ==== 可调节参数 ====
        const int kPreStartSeconds = 0;        // 提前多少秒启动录制
        const int kPostEndSeconds  = 0;       // 延后多少秒强制停止
        const int kStartHintWindow = 10 * 60;  // 开始前多少秒进入提示区间
        const int kEndHintWindow   = 10 * 60;  // 结束前多少秒进入提示区间
        const int kHintInterval    = 10;       // 提示间隔（秒）

        while (running_) {
            auto now = Clock::now();

            // 1. 用 occurrencesStartingWithin1h 做提示（不控制录制）
            auto upcoming = priv.manager->occurrencesStartingWithin1h(now);
            if (!upcoming.empty()) {
                auto nearest = *std::min_element(
                    upcoming.begin(), upcoming.end(),
                    [](const Occurrence& a, const Occurrence& b) {
                        return a.start < b.start;
                    });

                auto ms_to_start = duration_cast<milliseconds>(nearest.start - now).count();
                auto seconds_to_start = ms_to_start / 1000;

                // 倒计时提示
                if (seconds_to_start > 0 && seconds_to_start <= kStartHintWindow &&
                    (!priv.recordControl || priv.recordControl->state() == RecordControl::State::Ready)) {

                    if (lastStartHintTime.time_since_epoch().count() == 0 ||
                        duration_cast<seconds>(now - lastStartHintTime).count() >= kHintInterval) {

                        log::info("预计 %d 秒后开始录制 (课程 %s)",
                                  seconds_to_start, nearest.scheduleId.c_str());
                        lastStartHintTime = now;
                    }
                }
            }

            // 2. 用 statusAt 控制录制开始
            auto status = priv.manager->statusAt(now);

            if (status.busy &&
                (!priv.recordControl || priv.recordControl->state() == RecordControl::State::Ready)) {

                ensure_dir("/root/record_task");
                std::string filename = "/root/record_task/." + makeFileName(status.current->studentId, status.current->deviceId, now) + ".mp4";
                priv.recordControl->setFileName(filename);
                priv.recordControl->start();

                currentOcc_ = *status.current;
                expectedStopTime_ = currentOcc_->end + seconds(kPostEndSeconds);

                log::info("检测到课程进行中，启动录制: %s (课程 %s)",
                          filename.c_str(), currentOcc_->scheduleId.c_str());
            }

            // 3. 录制中
            if (priv.recordControl &&
                priv.recordControl->state() == RecordControl::State::Recording) {

                if (status.busy && status.current) {
                    // ====== 检查是否切换到新课程 ======
                    if (currentOcc_ && status.current->scheduleId != currentOcc_->scheduleId) {
                        double elapsed = priv.recordControl->duration();
                        log::info("课程切换: 停止上一课 %s (持续 %.2f 秒)，准备开始新课 %s",
                                  currentOcc_->scheduleId.c_str(), elapsed,
                                  status.current->scheduleId.c_str());

                        priv.recordControl->stop();

                        // 开启新课录制
                        ensure_dir("/root/record_task");
                        std::string filename = "/root/record_task/." + makeFileName(status.current->studentId, status.current->deviceId, now) + ".mp4";
                        priv.recordControl->setFileName(filename);
                        priv.recordControl->start();

                        currentOcc_ = *status.current;
                        expectedStopTime_ = currentOcc_->end + seconds(kPostEndSeconds);

                        log::info("新课程录制已启动: %s (课程 %s)",
                                  filename.c_str(), currentOcc_->scheduleId.c_str());
                    }
                    // ====== 时间有调整 ======
                    else if (status.current->end != currentOcc_->end) {
                        auto old_end = currentOcc_->end;
                        currentOcc_ = *status.current;
                        expectedStopTime_ = currentOcc_->end + seconds(kPostEndSeconds);

                        time_t old_ts = Clock::to_time_t(old_end);
                        time_t new_ts = Clock::to_time_t(currentOcc_->end);
                        char old_buf[64], new_buf[64];
                        strftime(old_buf, sizeof(old_buf), "%F %T", std::localtime(&old_ts));
                        strftime(new_buf, sizeof(new_buf), "%F %T", std::localtime(&new_ts));

                        log::warn("课程时间调整: 原结束 %s → 新结束 %s (课程 %s)",
                                  old_buf, new_buf, currentOcc_->scheduleId.c_str());
                    }

                    // ====== 提示预计结束时间 ======
                    auto seconds_to_end = duration_cast<seconds>(currentOcc_->end - now).count();
                    if (seconds_to_end > 0 && seconds_to_end <= kEndHintWindow) {
                        if (lastEndHintTime.time_since_epoch().count() == 0 ||
                            duration_cast<seconds>(now - lastEndHintTime).count() >= kHintInterval) {

                            time_t end_ts = Clock::to_time_t(currentOcc_->end);
                            char buf[64];
                            strftime(buf, sizeof(buf), "%F %T", std::localtime(&end_ts));

                            log::info("课程预计还有 %d 秒结束，预计结束时间: %s (课程 %s)",
                                      seconds_to_end, buf, currentOcc_->scheduleId.c_str());

                            lastEndHintTime = now;
                        }
                    }
                }
            }

            // 4. 停止录制逻辑：以缓存的 expectedStopTime_ 为准
            if (expectedStopTime_ && now >= *expectedStopTime_ &&
                priv.recordControl &&
                priv.recordControl->state() == RecordControl::State::Recording) {

                double elapsed = priv.recordControl->duration();
                log::info("到达预计结束时间，停止录制，持续: %.2f 秒", elapsed);
                priv.recordControl->stop();
                currentOcc_.reset();
                expectedStopTime_.reset();
            }

            std::this_thread::sleep_for(milliseconds(200));
        }
    }



} // namespace z
