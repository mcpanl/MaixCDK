#include "stdio.h"
#include "main.h"
#include "maix_util.hpp"
#include "maix_image.hpp"
#include "maix_time.hpp"
#include "maix_display.hpp"
#include "maix_rtsp.hpp"
#include "maix_wifi.hpp"
#include "maix_camera.hpp"
#include "maix_basic.hpp"
#include "maix_ffmpeg.hpp"
#include "sophgo_middleware.hpp"
#include "maix_pmu.hpp"
#include "csignal"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <cstdio>
#include <ctime>
#include <fstream>

#include <vector>
#include <mutex>
#include <map>
#include <set>
#include <thread>
#include <cstring>
#include <netinet/in.h>
#include <functional>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <queue>                 // 队列
#include <condition_variable>    // 条件变量
#include <atomic>                // 原子变量，用于线程安全状态标志
#include <optional>              // 可选类型，用于超时取数据
#include <chrono>                // 高精度时间
#include <sys/stat.h> 

#include <arpa/inet.h>

#include <iomanip>
#include <sstream>


#include "region.hpp"

#define ENABLE_DEBUG 0



#define UPDATE_LINE(fmt, ...) printf("\r" fmt, ##__VA_ARGS__); fflush(stdout)

#if ENABLE_DEBUG

// 获取当前时间字符串，格式为 HH:MM:SS.mmm
inline const char* current_time_str() {
    static char buffer[32];
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03lld",
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, static_cast<long long>(ms.count()));
    return buffer;
}

// 获取文件名，不带路径
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// 调试打印宏
#define DEBUG_PRINT(fmt, ...) \
    printf("[DEBUG][%s][%s:%d] " fmt "\n", current_time_str(), __FILENAME__, __LINE__, ##__VA_ARGS__)

#else
#define DEBUG_PRINT(fmt, ...) ((void)0)
#endif


using namespace maix;
using namespace maix::network;
using namespace ext_dev;

#define PORT 8090
#define ENABLE_RTSP 0
#define ENABLE_PIPE 0

int record_number = 0;
std::string custom_dir = "videos";

std::string local_ip = "0.0.0.0";
std::string broadcast_ip = "255.255.255.255";

int udp_w = 320;
int udp_h = 240;

int udp_port = 5006;

const char* get_current_time_string();

enum class RepeatType {
    Once = 0,
    Daily = 1,
    Weekly = 2,
    Monthly = 3
};

struct RecordingTask {
    std::string studentId;
    std::string startTimeStr;   // 格式 "YYYY-MM-DD HH:MM"
    int durationMinutes;
    RepeatType repeatType;
    std::string weekdayStr;     // "Monday", "Tuesday", ...
    int monthlyDay;
};

class TaskScheduler {
public:
    void addTask(const RecordingTask& task) {
        std::lock_guard<std::mutex> lock(taskMutex);
        tasks.push_back(task);
    }

    void printTaskList() {
        std::lock_guard<std::mutex> lock(taskMutex);

        const int idWidth = 13;
        const int timeWidth = 21;
        const int durationWidth = 10;
        const int repeatWidth = 11;
        const int weekdayWidth = 10;
        const int monthDayWidth = 12;

        auto repeatToStr = [](RepeatType rt) -> std::string {
            switch (rt) {
                case RepeatType::Once: return "Once";
                case RepeatType::Daily: return "Daily";
                case RepeatType::Weekly: return "Weekly";
                case RepeatType::Monthly: return "Monthly";
                default: return "Unknown";
            }
        };

        auto printLine = [&]() {
            std::cout
                << "+" << std::string(idWidth, '-')
                << "+" << std::string(timeWidth, '-')
                << "+" << std::string(durationWidth, '-')
                << "+" << std::string(repeatWidth, '-')
                << "+" << std::string(weekdayWidth, '-')
                << "+" << std::string(monthDayWidth, '-')
                << "+" << std::endl;
        };

        printLine();
        std::cout << "| " << std::left << std::setw(idWidth - 1) << "Student ID"
                  << "| " << std::setw(timeWidth - 1) << "Start Time"
                  << "| " << std::setw(durationWidth - 1) << "Duration"
                  << "| " << std::setw(repeatWidth - 1) << "Repeat"
                  << "| " << std::setw(weekdayWidth - 1) << "Weekday"
                  << "| " << std::setw(monthDayWidth - 1) << "Month Day"
                  << "|" << std::endl;
        printLine();

        for (const auto& task : tasks) {
            std::cout << "| " << std::left << std::setw(idWidth - 1) << task.studentId
                      << "| " << std::setw(timeWidth - 1) << task.startTimeStr
                      << "| " << std::setw(durationWidth - 1) << task.durationMinutes
                      << "| " << std::setw(repeatWidth - 1) << repeatToStr(task.repeatType)
                      << "| " << std::setw(weekdayWidth - 1) << task.weekdayStr
                      << "| " << std::setw(monthDayWidth - 1) << task.monthlyDay
                      << "|" << std::endl;
        }

        printLine();
    }

    void loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) {
            std::cerr << "无法打开任务文件: " << filename << std::endl;
            return;
        }

        std::string line;
        // 跳过表头
        if (!std::getline(file, line)) return;

        while (std::getline(file, line)) {
            std::istringstream ss(line);
            RecordingTask task;
            int repeatTypeInt;

            ss >> std::quoted(task.studentId);
            ss.ignore(1); // 跳过逗号

            ss >> std::quoted(task.startTimeStr);
            ss.ignore(1);

            ss >> task.durationMinutes;
            ss.ignore(1);

            ss >> repeatTypeInt;
            ss.ignore(1);

            ss >> std::quoted(task.weekdayStr);
            ss.ignore(1);

            ss >> task.monthlyDay;

            task.repeatType = static_cast<RepeatType>(repeatTypeInt);
            addTask(task);
        }
    }

    void saveToFile(const std::string& filename) {
        std::ofstream file(filename);
        if (!file) {
            std::cerr << "无法写入任务文件: " << filename << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(taskMutex);

        // 可选：写入表头
        file << "\"studentId\",\"startTimeStr\",durationMinutes,repeatType,\"weekdayStr\",monthlyDay\n";

        for (const auto& task : tasks) {
            file << std::quoted(task.studentId) << ","
                 << std::quoted(task.startTimeStr) << ","
                 << task.durationMinutes << ","
                 << static_cast<int>(task.repeatType) << ","
                 << std::quoted(task.weekdayStr) << ","
                 << task.monthlyDay << "\n";
        }
    }

    void run() {
        checkAndRunTasks();
        // while (true) {
        //     std::this_thread::sleep_for(std::chrono::seconds(800));
        // }
    }

private:
    std::vector<RecordingTask> tasks;
    std::mutex taskMutex;

    std::tm parseTime(const std::string& timeStr) {
        std::tm tm = {};
        std::istringstream ss(timeStr);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
        return tm;
    }

    int weekdayStringToInt(const std::string& weekday) {
        std::string wd = weekday;
        std::transform(wd.begin(), wd.end(), wd.begin(), ::tolower);

        if (wd == "sunday") return 0;
        if (wd == "monday") return 1;
        if (wd == "tuesday") return 2;
        if (wd == "wednesday") return 3;
        if (wd == "thursday") return 4;
        if (wd == "friday") return 5;
        if (wd == "saturday") return 6;
        return -1;
    }

    void checkAndRunTasks() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_t = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_t);

        std::lock_guard<std::mutex> lock(taskMutex);

        printf("** TASK RUNNING **\n");

        for (const auto& task : tasks) {
            std::tm task_tm = parseTime(task.startTimeStr);
            bool shouldRun = false;

            switch (task.repeatType) {
                case RepeatType::Once:
                    if (now_tm->tm_year == task_tm.tm_year &&
                        now_tm->tm_mon == task_tm.tm_mon &&
                        now_tm->tm_mday == task_tm.tm_mday &&
                        now_tm->tm_hour == task_tm.tm_hour &&
                        now_tm->tm_min == task_tm.tm_min) {
                        shouldRun = true;
                    }
                    break;

                case RepeatType::Daily:
                    if (now_tm->tm_hour == task_tm.tm_hour &&
                        now_tm->tm_min == task_tm.tm_min) {
                        shouldRun = true;
                    }
                    break;

                case RepeatType::Weekly:
                    if (now_tm->tm_wday == weekdayStringToInt(task.weekdayStr) &&
                        now_tm->tm_hour == task_tm.tm_hour &&
                        now_tm->tm_min == task_tm.tm_min) {
                        shouldRun = true;
                    }
                    break;

                case RepeatType::Monthly:
                    if (now_tm->tm_mday == task.monthlyDay &&
                        now_tm->tm_hour == task_tm.tm_hour &&
                        now_tm->tm_min == task_tm.tm_min) {
                        shouldRun = true;
                    }
                    break;
            }

            if (shouldRun) {
                std::thread(&TaskScheduler::executeTask, this, task).detach();
            }
        }
    }

    void executeTask(RecordingTask task) {
        std::cout << "🎥 开始录像: 学生ID = " << task.studentId
                  << ", 时长 = " << task.durationMinutes << " 分钟" << std::endl;

        std::this_thread::sleep_for(std::chrono::minutes(task.durationMinutes));

        std::cout << "✅ 录像结束: 学生ID = " << task.studentId << std::endl;
    }
};



template <typename T>
class DroppingQueue {
public:
    void push(T value) { // 接收一个值或指针副本，适配裸指针
        std::lock_guard<std::mutex> lock(mtx_);
        if (has_data_) {
            //printf("[DroppingQueue] Drop old data\n");
            // 如果是裸指针类型，调用者自己管理释放

        if constexpr (std::is_pointer<T>::value) {
            //printf("Well Delete\n");
            delete buffer_; // 释放旧数据
        }
        }
        buffer_ = value;
        has_data_ = true;
    }

    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!has_data_)
            return false;

        value = buffer_;
        has_data_ = false;
        return true;
    }

private:
    std::mutex mtx_;
    T buffer_;
    bool has_data_ = false;
};

std::string get_broadcast_address(const std::string& ip_str, const std::string& netmask_str)
{
    in_addr ip, netmask, broadcast;
    inet_aton(ip_str.c_str(), &ip);
    inet_aton(netmask_str.c_str(), &netmask);

    broadcast.s_addr = (ip.s_addr & netmask.s_addr) | (~netmask.s_addr);

    return std::string(inet_ntoa(broadcast));
}

std::string guess_netmask(const std::string& ip)
{
    if(ip.empty()) return "255.255.255.0"; // fallback

    int first_octet = std::stoi(ip.substr(0, ip.find('.')));
    int second_octet = std::stoi(ip.substr(ip.find('.') + 1, ip.find('.', ip.find('.') + 1)));

    if(first_octet == 10)
        return "255.0.0.0";
    else if(first_octet == 172 && (second_octet >= 16 && second_octet <= 31))
        return "255.240.0.0";
    else
        return "255.255.255.0"; // assume 192.168.x.x
}

void udp_broadcast_thread(DroppingQueue<std::vector<uint8_t>>& queue, std::atomic<bool>& running)
{
    broadcast_ip = "239.255.0.2";

    log::info("***** UDP IP *****");
    log::info(broadcast_ip.c_str());

    const char* BROADCAST_IP = broadcast_ip.c_str();
    const size_t MAX_PACKET_SIZE = 1400;
    const size_t HEADER_SIZE = 12;
    const size_t CHUNK_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return;
    }

    int broadcastEnable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        perror("setsockopt failed");
        close(sock);
        return;
    }

    in_addr_t local_ip_binary = inet_addr(local_ip.c_str());

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(udp_port);
    addr.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    uint32_t frame_id = 0;

    while (!app::need_exit() && running) {
        std::vector<uint8_t> jpeg_data;
        if (!queue.try_pop(jpeg_data)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        size_t jpeg_size = jpeg_data.size();
        uint16_t total_packets = (jpeg_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
        frame_id++;
        // printf("-- send jpeg %d, size = %zu bytes\n", frame_id, jpeg_size);

        for (uint16_t packet_id = 0; packet_id < total_packets; ++packet_id) {
            size_t start = packet_id * CHUNK_SIZE;
            size_t end = std::min(start + CHUNK_SIZE, jpeg_size);
            size_t payload_size = end - start;

            uint8_t buffer[MAX_PACKET_SIZE];

            // Header: frame_id (4 bytes) | total_packets (2 bytes) | packet_id (2 bytes)
            buffer[0] = (frame_id >> 24) & 0xFF;
            buffer[1] = (frame_id >> 16) & 0xFF;
            buffer[2] = (frame_id >> 8) & 0xFF;
            buffer[3] = frame_id & 0xFF;
            buffer[4] = (total_packets >> 8) & 0xFF;
            buffer[5] = total_packets & 0xFF;
            buffer[6] = (packet_id >> 8) & 0xFF;
            buffer[7] = packet_id & 0xFF;

            memcpy(buffer + 8, &local_ip_binary, 4);

            memcpy(buffer + HEADER_SIZE, jpeg_data.data() + start, payload_size);

            ssize_t sent = sendto(sock, buffer, HEADER_SIZE + payload_size, 0,
                                  (struct sockaddr*)&addr, sizeof(addr));
            if (sent < 0) {
                perror("UDP sendto failed");
            }

            
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    close(sock);
}

static std::vector<uint8_t> g_sps_pps_buf;


static struct {
    camera::Camera *cam;
    camera::Camera *cam2;
    display::Display *disp;
#if ENABLE_RTSP
    rtsp::Rtsp *rtsp;
#endif

    ffmpeg::FFmpegPacker *ffmpeg_packer;
    video::Encoder *encoder;
    audio::Recorder *audio_recorder;
    pmu::PMU *pmu;

    TaskScheduler *scheduler;

    Region *region;
    Region *region2;

    uint64_t last_read_pcm_ms;
    uint64_t last_read_cam_ms;
    uint64_t last_push_venc_ms;
    uint64_t video_pts;
    uint64_t audio_pts;

    uint64_t loop_last_ms;
    uint64_t last_update_region_ms;
    void *loop_last_frame;

    bool audio_en;


    bool video_stop_flag;

    std::string device_key;
    long disk_total;
    long disk_used;

    int bat_percent;
    bool is_charging;
    bool is_vbus_in;
} priv;


void task_worker_thread(std::atomic<bool>& running) {
    while (running) {
        if (priv.scheduler) {
            priv.scheduler -> run();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void cam2_worker_thread(DroppingQueue<image::Image*>& img_queue, std::atomic<bool>& running)
{
    // printf("^^ CAM2 WORKER RUN\n");
    uint64_t last_pmu_ms = time::ticks_ms();

    while(running) {
        // printf("^^ CAM2 LOOP\n");
        try {
            uint64_t curr_ms = time::ticks_ms();
            image::Image* disp_img = nullptr;
            image::Image* img = nullptr;

            if (priv.disp) {
                disp_img = new image::Image(priv.disp->width(), priv.disp->height(), image::FMT_RGB888);
            }

            if (priv.cam2) {
                // printf("^^ CAM2 READ\n");
                 img = priv.cam2->read();
                // printf("^^ CAM2 READ END\n");
            }

            if (disp_img) {
                const char* time_str = get_current_time_string();

                if (img) {
                    disp_img->draw_image(priv.disp->width() - img->width(), priv.disp->height() / 2 - img->height() / 2, *img);

                    img->draw_string(24, 24, time_str, image::COLOR_BLACK, 1, 1);
                    img->draw_string(24 - 1, 24 - 1, time_str, image::COLOR_WHITE, 1, 1);

                    if (priv.bat_percent) {
                        std::string bat_text = "[" + std::to_string(priv.bat_percent) + "%]";
                        if (priv.is_charging) {
                            img->draw_string(254, 24, bat_text, image::COLOR_BLACK, 1, 1);
                            img->draw_string(254 - 1, 24 - 1, bat_text, image::COLOR_GREEN, 1, 1);
                        } else {
                            img->draw_string(254, 24, bat_text, image::COLOR_BLACK, 1, 1);
                            img->draw_string(254 - 1, 24 - 1, bat_text, image::COLOR_WHITE, 1, 1);
                        }
                    }

                }


                priv.disp->show(*disp_img);
                delete disp_img;
            }

            if (img) {
                img_queue.push(img);  // 向队列插入图像
                // printf("^^ CAM2 pushed new image\n");
            }


            if (curr_ms - last_pmu_ms > 1000) {
                if (priv.pmu) {
                    priv.bat_percent = priv.pmu->get_bat_percent();
                    priv.is_charging = priv.pmu->is_charging();
                    priv.is_vbus_in = priv.pmu->is_vbus_in();

                    if (priv.bat_percent < 10) {
                        priv.pmu->set_bat_charging_cur(200);
                    } else if (priv.bat_percent < 25) {
                        priv.pmu->set_bat_charging_cur(400);
                    } else if (priv.bat_percent < 45) {
                        priv.pmu->set_bat_charging_cur(600);
                    } else if (priv.bat_percent < 60) {
                        priv.pmu->set_bat_charging_cur(800);
                    } else {
                        priv.pmu->set_bat_charging_cur(1000);
                    }
                }

                last_pmu_ms = curr_ms;
            }
        } catch (const err::Exception& e) {
            printf("\n^^ CAM2 READ ERROR!!!!!!!!!!\n");
        }


        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }
}

class RecorderManager
{
public:
    enum class RecordState { Idle, WaitingForSPS, Recording };

    RecorderManager(ffmpeg::FFmpegPacker *packer, decltype(priv) *priv_data)
        : packer_(packer), priv_(priv_data), recording_(false), state_(RecordState::Idle), start_time_ms_(0), file_path_("") {}

    // 设置录制文件名
    void setRecordFileName(const std::string &path)
    {
        file_path_ = path;
        if (packer_)
        {
            packer_->config2("path", file_path_.c_str());
        }
    }

    // 开始录制
    void startRecord()
    {
        if (state_ == RecordState::Recording)
            return;
        if (file_path_.empty())
            return;

        recording_ = true;
        state_ = RecordState::WaitingForSPS;

        // 复位时间戳
        priv_->last_read_pcm_ms = 0;
        priv_->last_read_cam_ms = 0;
        priv_->video_pts = 0;
        priv_->audio_pts = 0;

        if (priv_->audio_recorder)
        {
            priv_->audio_recorder->reset();
        }

        start_time_ms_ = maix::time::ticks_ms();
        std::cout << "Recording started.\n";
    }

    // 停止录制
    void stopRecord()
    {
        if (state_ == RecordState::Idle)
            return;

        if (packer_ && packer_->is_opened())
        {
            //packer_->close();
        }

        recording_ = false;
        state_ = RecordState::Idle;

        std::cout << "Recording stopped.\n";
    }

    // 是否处于录制状态
    bool isRecording() const { return recording_; }

    // 是否等待 SPS/PPS（等待打开 packer）
    bool isWaitingForSPS() const { return state_ == RecordState::WaitingForSPS; }

    // 设置状态为已经开始录制
    void setRecordingOpened()
    {
        state_ = RecordState::Recording;
    }

    // 是否已经打开 packer
    bool isOpened() const
    {
        return state_ == RecordState::Recording && packer_->is_opened();
    }

    // 获取录制时长
    uint64_t getRecordDuration() const
    {
        if (recording_)
        {
            return maix::time::ticks_ms() - start_time_ms_;
        }
        return 0;
    }

    // 获取当前文件大小
    uint64_t getRecordFileSize() const
    {
        if (file_path_.empty())
            return 0;

        struct stat stat_buf;
        if (stat(file_path_.c_str(), &stat_buf) == 0)
            return stat_buf.st_size;

        return 0;
    }

private:
    ffmpeg::FFmpegPacker *packer_;
    decltype(priv) *priv_; // 传入的 priv 指针
    bool recording_;
    RecordState state_;
    uint64_t start_time_ms_;
    std::string file_path_;
};


class TcpServer {
public:
    TcpServer() : server_fd(-1), running(false) {}

    ~TcpServer() {
        stop();
    }

    void start() {
        running = true;
        server_thread = std::thread(&TcpServer::run, this);
    }

    void stop() {
        printf("**** SERVER STOP ****\n");
        running = false;

        if (server_fd != -1) {
            close(server_fd);
            server_fd = -1;
        }

        {
            std::lock_guard<std::mutex> lock(client_mutex);
            for (auto& [fd, client] : clients) {
                printf("    **** wait client stop...\n");
                client->stop();
            }
            clients.clear();
        }
#if 0
        if (server_thread.joinable()) {
            server_thread.join();
        }
#endif
    }

    void setMessageCallback(std::function<void(int, const std::vector<char>&)> callback) {
        onMessage = callback;
    }

    void broadcastText(const std::string& message) {
        std::vector<char> data(message.begin(), message.end());
        broadcastBinary(data);
    }

    void broadcastBinary(const std::vector<char>& data) {
        std::lock_guard<std::mutex> lock(client_mutex);
        for (auto& [fd, client] : clients) {
            client->sendData(data);
        }
    }

private:
    struct ClientHandler {
        int fd;
        std::thread thread;

        std::thread readThread;
        std::thread writeThread;

        std::queue<std::vector<char>> sendQueue;
        std::mutex queueMutex;
        std::condition_variable queueCond;
        std::atomic<bool> active;
        std::function<void(int, const std::vector<char>&)> onMessage;

        ClientHandler(int client_fd, std::function<void(int, const std::vector<char>&)> messageCallback)
            : fd(client_fd), active(true), onMessage(std::move(messageCallback)) {}

        void start() {
            //thread = std::thread(&ClientHandler::runSelectLoop, this);
            readThread = std::thread(&ClientHandler::readLoop, this);
            writeThread = std::thread(&ClientHandler::writeLoop, this);
        }

        void stop() {

            printf("**** CLIENT HANDLER STOP ****\n");

            active = false;
            shutdown(fd, SHUT_RDWR);
            queueCond.notify_all();

            if (readThread.joinable()) readThread.join();
            if (writeThread.joinable()) writeThread.join();
#if 0
            if (thread.joinable()) {
                thread.join();
            }
#endif
            close(fd);
        }

        void sendData(const std::vector<char>& data) {
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                sendQueue.push(data);
            }
            queueCond.notify_one();
        }

void runSelectLoop() {
    // 设置为非阻塞（可选，但推荐）
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    while (!app::need_exit() && active) {
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        FD_SET(fd, &read_fds);
        if (!sendQueue.empty()) {
            FD_SET(fd, &write_fds);
        }

        timeval timeout = {1, 0}; // 1 秒超时
        int max_fd = fd;

        int ret = select(max_fd + 1, &read_fds, &write_fds, nullptr, &timeout);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "select error on fd: " << fd << std::endl;
            break;
        }

        if (FD_ISSET(fd, &read_fds)) {
            char buffer[1024];
            ssize_t bytes_read = recv(fd, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) {
                std::cout << "Client disconnected: " << fd << std::endl;
                break;
            }

            if (onMessage) {
                std::vector<char> data(buffer, buffer + bytes_read);
                onMessage(fd, data);
            }
        }

        if (FD_ISSET(fd, &write_fds)) {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (!sendQueue.empty()) {
                auto msg = sendQueue.front();
                ssize_t sent = send(fd, msg.data(), msg.size(), 0);
                if (sent < 0) {
                    std::cerr << "Send error on client: " << fd << std::endl;
                    break;
                }
                sendQueue.pop();
            }
        }
    }

    active = false;
    queueCond.notify_all();
}

void readLoop() {
    while (!app::need_exit() && active) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        int ret = select(fd + 1, &read_fds, nullptr, nullptr, nullptr);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[readLoop] select error\n";
            break;
        }

        if (FD_ISSET(fd, &read_fds)) {
            char buffer[1024];
            ssize_t bytes_read = recv(fd, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) {
                std::cout << "Client disconnected: " << fd << std::endl;
                break;
            }

            if (onMessage) {
                std::vector<char> data(buffer, buffer + bytes_read);
                onMessage(fd, data);
            }
        }
    }

    active = false;
    queueCond.notify_all();  // wake writeLoop if needed
}

void writeLoop() {
    while (!app::need_exit() && active) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCond.wait(lock, [this]() {
            return !sendQueue.empty() || !active || app::need_exit();
        });

        while (!sendQueue.empty() && active && !app::need_exit()) {
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(fd, &write_fds);

            lock.unlock();  // 解锁以避免 select 被互斥阻塞
            int ret = select(fd + 1, nullptr, &write_fds, nullptr, nullptr);
            lock.lock();

            if (ret < 0) {
                if (errno == EINTR) continue;
                std::cerr << "[writeLoop] select error\n";
                break;
            }

            if (FD_ISSET(fd, &write_fds)) {
                auto msg = sendQueue.front();
                ssize_t sent = send(fd, msg.data(), msg.size(), 0);
                if (sent < 0) {
                    std::cerr << "Send error on client: " << fd << std::endl;
                    active = false;
                    break;
                }

                sendQueue.pop();
            }
        }
    }
}

    };

    int server_fd;
    std::thread server_thread;
    std::unordered_map<int, std::shared_ptr<ClientHandler>> clients;
    std::mutex client_mutex;
    std::atomic<bool> running;

    std::function<void(int, const std::vector<char>&)> onMessage;

    void run() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Socket creation failed\n";
            return;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Bind failed\n";
            close(server_fd);
            return;
        }

        if (listen(server_fd, SOMAXCONN) < 0) {
            std::cerr << "Listen failed\n";
            close(server_fd);
            return;
        }

        std::cout << "Server listening on port " << PORT << "...\n";

        while (!app::need_exit() && running) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                std::cout << "New client connected: " << client_fd << "\n";

                auto client = std::make_shared<ClientHandler>(client_fd, onMessage);
                {
                    std::lock_guard<std::mutex> lock(client_mutex);
                    clients[client_fd] = client;
                }
                client->start();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(125));  // avoid busy waiting
            }
        }

        std::cout << "Server stopped accepting new clients.\n";
    }
};


std::shared_ptr<TcpServer> server;


void jpeg_worker_thread(DroppingQueue<image::Image*>& img_queue,
                        DroppingQueue<std::vector<uint8_t>>& jpeg_queue,
                        std::atomic<bool>& running)
{
    DEBUG_PRINT("jpeg_worker_thread start");

    while (!app::need_exit() && running) {
        image::Image* img = nullptr;
        if (!img_queue.try_pop(img)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        if (!img) {
            printf("jpeg_worker: got nullptr image, skipping\n");
            continue;
        }


        // image::Image* udp_img = nullptr;
        // udp_img = img->resize(udp_w, udp_h, image::Fit::FIT_FILL, image::ResizeMethod::BILINEAR);

        // delete img;

        // if (!udp_img) {
            // printf("jpeg_worker: got nullptr udp_image, skipping\n");
            // continue;
        // }

        // DEBUG_PRINT("jpeg_worker: got udp_image %p", img);
        // DEBUG_PRINT("jpeg_worker: got udp_image of size %zu", udp_img->data_size());

        image::Image* jpg_img = img->to_format(image::FMT_JPEG);
        DEBUG_PRINT("jpeg_worker: formated image %p", jpg_img);
        DEBUG_PRINT("jpeg_worker: formated jpeg of size %zu", jpg_img->data_size());

        // printf("JPEG SIZE: %zu\n", jpg_img->data_size());

        if (!jpg_img) {
            printf("jpeg_worker: to_format failed\n");
            delete img;
            continue;
        }

        uint8_t* jpeg_data = static_cast<uint8_t*>(jpg_img->data());
        size_t jpeg_size = jpg_img->data_size();

        std::vector<uint8_t> jpeg(jpeg_data, jpeg_data + jpeg_size);
        jpeg_queue.push(std::move(jpeg));
/*
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "JPEG_SIZE = %d\n", jpeg_size);

        server->broadcastText(buffer);
*/

        delete jpg_img;
        delete img;

        std::this_thread::sleep_for(std::chrono::milliseconds(125));
    }
}



const char* get_current_time_string() {
    static char buffer[20];
    ::time_t rawtime;
    ::tm *timeinfo;

    ::time(&rawtime);
    timeinfo = ::localtime(&rawtime);

    ::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return buffer;
}



static int _mmf_vi_frame_pop(int ch, void **frame_info,  mmf_frame_info_t *frame_info_mmap, int block_ms) {
    if (frame_info == NULL || frame_info_mmap == NULL) {
        printf("invalid param\n");
        return -1;
    }

    int ret = -1;
    VIDEO_FRAME_INFO_S *frame = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
    memset(frame, 0, sizeof(VIDEO_FRAME_INFO_S));
    if ((ret = CVI_VPSS_GetChnFrame(0, ch, frame, (CVI_S32)block_ms)) == 0) {
        int image_size = frame->stVFrame.u32Length[0]
            + frame->stVFrame.u32Length[1]
            + frame->stVFrame.u32Length[2];
        CVI_VOID *vir_addr;
        vir_addr = CVI_SYS_MmapCache(frame->stVFrame.u64PhyAddr[0], image_size);
        CVI_SYS_IonInvalidateCache(frame->stVFrame.u64PhyAddr[0], vir_addr, image_size);

        frame->stVFrame.pu8VirAddr[0] = (CVI_U8 *)vir_addr;		// save virtual address for munmap
        frame_info_mmap->data = vir_addr;
        frame_info_mmap->len = image_size;
        frame_info_mmap->w = frame->stVFrame.u32Width;
        frame_info_mmap->h = frame->stVFrame.u32Height;
        frame_info_mmap->fmt = frame->stVFrame.enPixelFormat;
    } else {
        free(frame);
        frame = NULL;
    }

    if (frame_info) {
        *frame_info = frame;
    }
    return ret;
}

static void _mmf_vi_frame_free(int ch, void **frame_info)
{
    if (!frame_info || !*frame_info) {
        return;
    }

    VIDEO_FRAME_INFO_S *frame = (VIDEO_FRAME_INFO_S *)*frame_info;
    int image_size = frame->stVFrame.u32Length[0]
        + frame->stVFrame.u32Length[1]
        + frame->stVFrame.u32Length[2];
    CVI_SYS_Munmap(frame->stVFrame.pu8VirAddr[0], image_size);
    if (CVI_VPSS_ReleaseChnFrame(0, ch, frame) != 0) {
        SAMPLE_PRT("CVI_VI_ReleaseChnFrame NG\n");
    }

    free(*frame_info);
    *frame_info = NULL;
}


std::string formatRecordTime(int milliseconds) {
    int totalSeconds = milliseconds / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;

    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << minutes
        << ":" 
        << std::setw(2) << std::setfill('0') << seconds;
    
    return oss.str();
}


int _main(int argc, char* argv[])
{
    mmf_deinit_v2(true);

    priv.device_key = sys::device_key();
    printf("DEVICE KEY = %s\n", priv.device_key.c_str());

    std::map<std::string, unsigned long long> disk_usage = sys::disk_usage("/");
    if (disk_usage.find("total") != disk_usage.end()) {
        priv.disk_total = disk_usage["total"];
    }

    if (disk_usage.find("used") != disk_usage.end()) {
        priv.disk_used = disk_usage["used"];
    }

    printf("DISK %lld / %lld\n", priv.disk_used, priv.disk_total);


    priv.pmu = new pmu::PMU("axp2101");

    priv.scheduler = new TaskScheduler();

    RecordingTask task;
    task.studentId = "S201901";
    task.startTimeStr = "2025-08-04 04:59";
    task.durationMinutes = 2;
    task.repeatType = RepeatType::Once;
    task.weekdayStr = "Monday";
    task.monthlyDay = 0;

    priv.scheduler->addTask(task);
    priv.scheduler->saveToFile("/root/s.csv");

    priv.scheduler->printTaskList();

    std::vector<std::string> wifi_ifaces = wifi::list_devices();
    for(auto &iface : wifi_ifaces)
    {
        log::info("wifi iface: %s", iface.c_str());
    }
    if(wifi_ifaces.empty())
    {
        log::error("no wifi iface found");
        return -1;
    }
    wifi::Wifi wifi(wifi_ifaces[0]);

    std::string netmask = guess_netmask(wifi.get_ip());
    
    log::info("IP: %s", wifi.get_ip().c_str());
    log::info("MAC: %s", wifi.get_mac().c_str());
    log::info("Gateway: %s", wifi.get_gateway().c_str());
    log::info("MASK: %s", netmask.c_str());

    local_ip = wifi.get_ip();
    broadcast_ip = get_broadcast_address(wifi.get_ip(), netmask);

    DroppingQueue<image::Image*> img_queue;
    //DroppingQueue<image::Image*> img_queue;
    DroppingQueue<std::vector<uint8_t>> jpeg_queue;
    std::atomic<bool> running = true;

    server = std::make_shared<TcpServer>();

    server->start();


    // 启动 JPEG 转换线程
    std::thread jpeg_thread(jpeg_worker_thread, std::ref(img_queue), std::ref(jpeg_queue), std::ref(running));

    // 启动 UDP 线程
    std::thread udp_thread(udp_broadcast_thread, std::ref(jpeg_queue), std::ref(running));

    // 启动线程
    std::thread cam2_thread(cam2_worker_thread, std::ref(img_queue), std::ref(running));

    // 启动线程
    std::thread task_thread(task_worker_thread, std::ref(running));

#if ENABLE_PIPE
    printf("Open PIPE\n");
    pipe_fd = open("/root/stream.h264", O_WRONLY);
    printf("Open PIPE DONE\n");
#endif


/*
    server.setMessageCallback([](int client_fd, const std::vector<char>& data) {
        std::string msg(data.begin(), data.end());
        std::cout << "Received from " << client_fd << ": " << msg << std::endl;
    });

    server.start();
*/

    int cam_w = 1920;
    // int cam_w = 1920;
    int cam_h = 1080;
    // int cam_h = 1080;
    int cam2_w = 320;
    int cam2_h = 240;

    image::Format cam_fmt = image::Format::FMT_YVU420SP;
    image::Format cam2_fmt = image::Format::FMT_RGB888;
    int cam_fps = 24;
    int cam_buffer_num = 6;
    int cam_bitrate = 7 * 1000 * 1000;

    priv.audio_en = true;
    priv.video_stop_flag = false;

    priv.cam = new camera::Camera(cam_w, cam_h, cam_fmt, "", cam_fps, cam_buffer_num);
    priv.cam2 = priv.cam->add_channel(cam2_w, cam2_h, cam2_fmt, cam_fps, cam_buffer_num);

/*
            auto string_size = image::string_size("0000-00-00 00:00:00");
            auto region_w = 600;
            auto region_h = 300;
            auto region_x = 0;
            auto region_y = 0;
            priv.region = new Region(region_x, region_y, region_w, region_h, image::FMT_BGRA8888, priv.cam);
            err::check_null_raise(priv.region, "region open failed");
*/

    priv.disp = new display::Display();

    printf("[U] init audio_recorder\n");
    priv.audio_recorder = new audio::Recorder();
    printf("[U] init audio_recorder success!\n");
    err::check_null_raise(priv.audio_recorder, "audio recorder init failed!");


    // Init Encoder
    priv.encoder = new video::Encoder("", cam_w, cam_h, image::Format::FMT_YVU420SP, video::VideoType::VIDEO_H264, cam_fps, 50, cam_bitrate, 1000, false, true, 1);

    // Init FFmpeg Packer
    priv.ffmpeg_packer = new ffmpeg::FFmpegPacker();
    err::check_null_raise(priv.ffmpeg_packer, "ffmpeg packer init failed");
    err::check_bool_raise(!priv.ffmpeg_packer->config("has_video", true), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_codec_id", AV_CODEC_ID_H264), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_width", cam_w), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_height", cam_h), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_bitrate", cam_bitrate), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_fps", cam_fps), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_pixel_format", AV_PIX_FMT_NV21), "rtmp config failed!");

    err::check_bool_raise(!priv.ffmpeg_packer->config("has_audio", priv.audio_en), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_sample_rate", 48000), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_channels", 1), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_bitrate", 128000), "rtmp config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_format", AV_SAMPLE_FMT_S16), "rtmp config failed!");

    RecorderManager recorderManager(priv.ffmpeg_packer, &priv);

    server->setMessageCallback([&](int client_fd, const std::vector<char>& data) {
        std::string msg(data.begin(), data.end());
        std::cout << "Received from " << client_fd << ": " << msg << std::endl;

        if (msg.rfind("set ", 0) == 0) {
            // set /root/videos/test.mp4
            std::string filename = msg.substr(4);
            recorderManager.setRecordFileName(filename);
            server->broadcastText("File name set to: " + filename + "\n");
        } else if (msg == "start") {
            priv.video_stop_flag = false;
            recorderManager.startRecord();
            server->broadcastText("Recording started.\n");
        } else if (msg == "stop") {
            priv.video_stop_flag = true;
            recorderManager.stopRecord();
            server->broadcastText("Recording stopped.\n");
        } else if (msg == "info") {
            std::string status = recorderManager.isRecording() ? "Recording" : "Stopped";
            std::string duration = std::to_string(recorderManager.getRecordDuration()) + " ms";
            std::string filesize = std::to_string(recorderManager.getRecordFileSize()) + " bytes";

            std::string info = 
                "Status: " + status + "\n" +
                "Duration: " + duration + "\n" +
                "File Size: " + filesize + "\n";

            server->broadcastText(info);
        } else if (msg == "status") {
            std::string status = recorderManager.isRecording() ? "Recording\n" : "Stopped\n";
            server->broadcastText("Status: " + status + "\n");
        } else if (msg == "duration") {
            server->broadcastText("Duration: " + std::to_string(recorderManager.getRecordDuration()) + " ms\n");
        } else if (msg == "filesize") {
            server->broadcastText("File Size: " + std::to_string(recorderManager.getRecordFileSize()) + " bytes\n");
        } else {
            server->broadcastText("Unknown command.\n");
        }
    });


    // 创建目录
    std::string dir_path = "/root/" + custom_dir;
    mkdir(dir_path.c_str(), 0755);
    
    // 构造文件名
    std::string output_path = "/root/" + custom_dir + "/" + std::to_string(record_number) + ".mp4";
    printf("OutputPath = %s\n", output_path.c_str());
    priv.ffmpeg_packer->config2("path", output_path.c_str());

    // priv.ffmpeg_packer->open();

#if ENABLE_RTSP
    priv.rtsp = new rtsp::Rtsp();
    priv.rtsp->bind_camera(priv.cam2);
    priv.rtsp->bind_audio_recorder(&audio_recorder);
    rtsp::Region *region = priv.rtsp->add_region(0, 0, 256, 64);

    image::Image *rgn_img;

    log::info("url:%s", priv.rtsp->get_url().c_str());
    std::vector<std::string> url = priv.rtsp->get_urls();
    for (size_t i = 0; i < url.size(); i ++) {
        log::info("url[%d]:%s", i, url[i].c_str());
    }
    err::check_raise(priv.rtsp->start());
#endif

    uint64_t total_audio_samples_sent = 0;

    uint64_t last_ms = time::ticks_ms();
    int cnt = 0;
    while(!app::need_exit()) {
//printf("=================================\n");
#if ENABLE_RTSP
        rgn_img = region->get_canvas();
        const char* time_str = get_current_time_string();
        rgn_img->draw_string(24, 24, time_str, image::COLOR_WHITE);
        region->update_canvas();
        delete rgn_img;
#endif


        maix::image::Image *img = nullptr;

#if 0
        try {
            img = priv.cam2->read();
        } catch (std::exception &e) {
            time::sleep_ms(10);
            continue;
        }
#endif

        bool found_venc_stream = false;
        void *frame = NULL;
        mmf_frame_info_t f;

//printf("*** from vi get data\n");


        const char* time_str = get_current_time_string();

        if (priv.region) {
            if(time::ticks_ms() - priv.last_update_region_ms > 800) {
                printf("******* REGION *********\n");
                auto img = priv.region->get_canvas();
                img->draw_string(24, 24, time_str, image::COLOR_BLACK, 2);
                img->draw_string(22, 22, time_str, image::COLOR_WHITE, 2);
                priv.region->update_canvas();
                priv.last_update_region_ms = time::ticks_ms();
            }
        }

        // 从VI获取一帧数据
        int ch = priv.cam->get_channel();
        int res = _mmf_vi_frame_pop(ch, &frame, &f, 40);

        DEBUG_PRINT("frame vi pop %s, ptr = %p", res == 0 ? "success" : "fail", frame);

        if (res != 0 || frame == nullptr) {
            printf("Failed to get frame, skipping...\n");
            time::sleep_ms(10);
            continue;
        }


DEBUG_PRINT("*** push to venc\n");
        // 将帧数据推送至VENC
        int res2 = mmf_venc_push2(1, frame);
        DEBUG_PRINT("frame venc push %s", res2 == 0 ? "success" : "fail");

DEBUG_PRINT("*** from venc get data\n");
        // 从编码器通道1取出编码数据
        mmf_stream_t venc_stream = {0};
        if (0 == mmf_venc_pop(1, &venc_stream)) {
            if (venc_stream.count > 0) {
                found_venc_stream = true;
            }
        }

DEBUG_PRINT("*** config pts sync\n");
        // 配置视频PTS与音频PTS同步
        if (priv.ffmpeg_packer && recorderManager.isOpened()) {
            double temp_us = priv.ffmpeg_packer->video_pts_to_us(priv.video_pts);
            priv.audio_pts = priv.ffmpeg_packer->audio_us_to_pts(temp_us);
        }

        // printf("*** 取出的编码数据: %d \n", venc_stream.count);


DEBUG_PRINT("*** found venc stream?\n");
        if (found_venc_stream) {
            // 如果 packer 没打开且是第一帧（含SPS/PPS），配置 SPS/PPS 并打开
            if (priv.ffmpeg_packer && recorderManager.isWaitingForSPS()) {
                if (venc_stream.count > 1) {

                    printf("***** CONFIG SPS/PPS *****\n");

                    int sps_pps_size = venc_stream.data_size[0] + venc_stream.data_size[1];
                    uint8_t *sps_pps = (uint8_t *)malloc(sps_pps_size);
                    if (sps_pps) {
                        memcpy(sps_pps, venc_stream.data[0], venc_stream.data_size[0]);
                        memcpy(sps_pps + venc_stream.data_size[0], venc_stream.data[1], venc_stream.data_size[1]);

                        int sps_pps_size = venc_stream.data_size[0] + venc_stream.data_size[1];
                        g_sps_pps_buf.resize(sps_pps_size);
                        memcpy(g_sps_pps_buf.data(), venc_stream.data[0], venc_stream.data_size[0]);
                        memcpy(g_sps_pps_buf.data() + venc_stream.data_size[0], venc_stream.data[1], venc_stream.data_size[1]);


                        if (0 == priv.ffmpeg_packer->config_sps_pps(sps_pps, sps_pps_size)) {
                            while (0 != priv.ffmpeg_packer->open() && !app::need_exit()) {
                                time::sleep_ms(500);
                                log::info("Can't open ffmpeg, retry again...");
                            }

                            recorderManager.setRecordingOpened(); 

                            if (priv.audio_recorder) {
                                priv.audio_recorder->reset();
                            }

                            total_audio_samples_sent = 0;

                            priv.last_read_cam_ms = 0;
                            priv.video_pts = 0;
                            priv.audio_pts = 0;
                        }
                        free(sps_pps);
                    }
                }
            }

DEBUG_PRINT("*** push video to packeter\n");
            // 如果 packer 已经打开，把编码数据推进去
            if (recorderManager.isOpened()) {
                
                //printf("^^^^^^^ 包装器已打开，准备推送数据\n");
            
                uint8_t *data = NULL;
                int data_size = 0;
                if (venc_stream.count == 1) {
                    data = venc_stream.data[0];
                    data_size = venc_stream.data_size[0];
                } else if (venc_stream.count > 1) {
                    data = venc_stream.data[2]; // 跳过 SPS/PPS，取 IDR/P帧
                    data_size = venc_stream.data_size[2];
                }

                if (data_size) {
                    // PTS 计算
                    if (priv.last_read_cam_ms == 0) {
                        priv.video_pts = 0;
                        priv.last_read_cam_ms = time::ticks_ms();
                    } else {
                        priv.video_pts += priv.ffmpeg_packer->video_us_to_pts(
                                (time::ticks_ms() - priv.last_read_cam_ms) * 1000
                                );
                        priv.last_read_cam_ms = time::ticks_ms();
                    }

                    // 推入 ffmpeg 打包器
                    if (err::ERR_NONE != priv.ffmpeg_packer->push(data, data_size, priv.video_pts)) {
                        log::error("ffmpeg push failed!");
                    }
                }
            }


DEBUG_PRINT("*** push audio to packeter\n");
            if (priv.audio_en && recorderManager.isOpened()) { 
                //printf("Audio is enabled and ffmpeg_packer is opened.\n"); // Check if audio is enabled and ffmpeg_packer is opened.

                int frame_size_per_second = priv.ffmpeg_packer->get_audio_frame_size_per_second();
                //printf("Frame size per second: %d\n", frame_size_per_second); // Output the frame size per second.

                uint64_t loop_ms = 0;
                int read_pcm_size = 0;

                if (priv.last_read_pcm_ms == 0) {
                    loop_ms = 41;
                    read_pcm_size = frame_size_per_second * loop_ms * 1.5 / 1000;
                    priv.audio_pts = 0;
                    priv.last_read_pcm_ms = time::ticks_ms();
                    //printf("First read: loop_ms = %llu, read_pcm_size = %d\n", loop_ms, read_pcm_size); // Debug first read condition.
                } else {
                    loop_ms = time::ticks_ms() - priv.last_read_pcm_ms;
                    priv.last_read_pcm_ms = time::ticks_ms();
                    //printf("Subsequent read: loop_ms = %llu\n", loop_ms); // Debug subsequent read condition.

                    read_pcm_size = frame_size_per_second * loop_ms * 1.5 / 1000;
                    priv.audio_pts += priv.ffmpeg_packer->audio_us_to_pts(loop_ms * 1000);
                    //printf("read_pcm_size = %d, audio_pts = %llu\n", read_pcm_size, priv.audio_pts); // Debug PCM size and audio PTS.
                }

                auto remain_frame_count = priv.audio_recorder->get_remaining_frames();
                //printf("Remaining frames: %llu\n", remain_frame_count); // Output the remaining frames.

                auto bytes_per_frame = priv.audio_recorder->frame_size();
                //printf("Bytes per frame: %d\n", bytes_per_frame); // Output the bytes per frame.

                auto remain_frame_bytes = remain_frame_count * bytes_per_frame;
                //printf("Remaining frame bytes: %llu\n", remain_frame_bytes); // Output the remaining frame bytes.

/*
read_pcm_size = remain_frame_bytes;
read_pcm_size = std::max(read_pcm_size, 1024);
read_pcm_size = (read_pcm_size + 1023) & ~1023;
*/
                //read_pcm_size = (read_pcm_size + 1023) & ~1023;
                //printf("Adjusted read_pcm_size: %d\n", read_pcm_size); // Debug adjusted PCM size.

                if (read_pcm_size > remain_frame_bytes) {
                    read_pcm_size = remain_frame_bytes;
                    //printf("Read PCM size exceeded remaining bytes, adjusted to: %d\n", read_pcm_size); // Debug if adjustment happens.
                }

                Bytes *pcm_data = priv.audio_recorder->record_bytes(read_pcm_size);
                if (pcm_data && pcm_data->data) {
                    //printf("Recorded PCM data: data_len = %d\n", pcm_data->data_len); // Debug PCM data length.
                    //printf("pcm_data->data = %p, data_len = %d\n", pcm_data->data, pcm_data->data_len);
                    if (pcm_data->data_len > 0) {
/*
int samples_this_frame = read_pcm_size / 2;  // mono * 16bit = 2 bytes/sample

uint64_t sample_rate = 48000;
uint64_t time_base = 1000000;

priv.audio_pts = total_audio_samples_sent * time_base / sample_rate;

printf("[AUDIO] pts: %d, pcm_len: %d\n", priv.audio_pts, pcm_data->data_len);
*/
                        // log::info("[AUDIO] pts:%d  pts %f s", priv.audio_pts, priv.ffmpeg_packer->audio_pts_to_us(priv.audio_pts) / 1000000);
                        if (err::ERR_NONE != priv.ffmpeg_packer->push(pcm_data->data, pcm_data->data_len, priv.audio_pts, true)) {
                            log::error("ffmpeg push failed!");
                            printf("ffmpeg push failed!\n"); // Debug ffmpeg push failure.
                        } else {
                           // printf("ffmpeg push succeeded.\n"); // Debug ffmpeg push success.
                        }
    // 更新累计
    //total_audio_samples_sent += samples_this_frame;
                    }
                    delete pcm_data;
                    //printf("PCM data deleted.\n"); // Debug PCM data deletion.
                } else {
                    printf("Failed to record PCM data.\n"); // Debug PCM data recording failure.
                }
            }

        }


DEBUG_PRINT("*** release venc\n");
        // 释放VENC资源
        mmf_venc_free(1);

        // 将帧数据推送至VO
        // mmf_vo_frame_push2(0, 0, 2, frame);

DEBUG_PRINT("*** release frame\n");
        // 释放帧数据
        _mmf_vi_frame_free(ch, &frame);
#if 0
        img_queue.push(img);
        priv.disp->show(*img);
#endif

        delete img;


/*
image::Image* disp_img = new image::Image(priv.disp->width(), priv.disp->height(), image::FMT_RGB888);


DEBUG_PRINT("disp_img format=%d", disp_img->format());


DEBUG_PRINT("*** read from cam2\n");
image::Image* img2 = priv.cam2->read();


img2->draw_string(170, 420, time_str, image::COLOR_BLACK, 1.5, 2);
img2->draw_string(170 - 2, 420 - 2, time_str, image::COLOR_WHITE, 1.5, 2);


DEBUG_PRINT("img2 read ptr = %p, format=%d", img2, img2->format());
if(img2) {
    DEBUG_PRINT("*** img_queue push img2\n");
    
    disp_img->draw_image(priv.disp->width() - img2->width(), priv.disp->height() / 2 - img2->height() / 2, *img2);

    if (recorderManager.isRecording()) {
        disp_img->draw_string(68, 36, formatRecordTime(recorderManager.getRecordDuration()).c_str(), image::COLOR_RED, 2, 2);       
    } else {
        disp_img->draw_string(68, 36, "Ready", image::COLOR_GREEN, 2, 2);
    }

    // disp_img->draw_string(0, 0, time_str, image::Color::from_rgb(255,255,255));

    // img_queue.push(img2);
}

// disp_img->draw_rect(60, 0, 640, 60, image::Color::from_rgb(200, 200, 200), -1);

//printf("*** disp show img2\n");
priv.disp->show(*disp_img);
delete disp_img;
if(img2) {
    delete img2;
}

        //image::Image *img2 = priv.cam2->read();    
        //priv.disp->show(*img2);
        //img_queue.push(std::move(img2));

//std::unique_ptr<image::Image> img2(priv.cam2->read());
//priv.disp->show(*img2);
//img_queue.push(std::move(img2)); // 自动管理内存
*/

if(priv.video_stop_flag) {
    printf("*** 主循环触发停止录像 ***\n");
    if (priv.ffmpeg_packer) {
        priv.ffmpeg_packer->close();
        printf("*** 包装器关闭完成\n");
    }
    printf("*** 主循环触发停止录像 完毕 ***\n");
    priv.video_stop_flag = false;
}

#if 0
        image::Image *img2 = priv.cam2->read();
        image::Image *jpg_img = img2->to_format(image::FMT_JPEG);

        priv.disp->show(*img2);

        uint8_t* jpeg_data = (uint8_t*)jpg_img->data();
        size_t jpeg_size = jpg_img->data_size();

    
        // 主线程中不断 push 最新 jpeg 数据
        std::vector<uint8_t> jpeg(jpeg_data, jpeg_data + jpeg_size);
        jpeg_queue.push(std::move(jpeg)); // 用移动语义，避免拷贝


        delete img2;
#endif
        uint64_t curr_ms = time::ticks_ms();

        const uint64_t target_frame_time = 1000 / 24; // 24fps ≈ 41ms per frame
        uint64_t elapsed = curr_ms - last_ms;

        UPDATE_LINE("* loop at %lld ms, target %lld ms, sleep %lld ms", curr_ms - last_ms, target_frame_time, target_frame_time - elapsed);

        if (elapsed < target_frame_time) {
            time::sleep_ms(target_frame_time - elapsed);  // 延迟补足帧时间
        }

        last_ms = time::ticks_ms();  // 更新为真正进入下一帧的时间
    }

#if ENABLE_RTSP
    priv.rtsp->stop();
#endif

    running = false;
    jpeg_thread.join();
    udp_thread.join();

    server->stop();
    std::cout << "Server stopped.\n";

    printf("准备关闭视频流\n");

    if (priv.region) {
        delete priv.region;
        priv.region = nullptr;
    }

    priv.ffmpeg_packer->close();
    delete priv.encoder;
    delete priv.ffmpeg_packer;
#if ENABLE_RTSP
    delete priv.rtsp;
#endif
    delete priv.cam2;
    delete priv.cam;
    delete priv.pmu;
    delete priv.scheduler;
    return 0;
}

int main(int argc, char* argv[])
{
    // Catch signal and process
    sys::register_default_signal_handle();

    // Use CATCH_EXCEPTION_RUN_RETURN to catch exception,
    // if we don't catch exception, when program throw exception, the objects will not be destructed.
    // So we catch exception here to let resources be released(call objects' destructor) before exit.
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
