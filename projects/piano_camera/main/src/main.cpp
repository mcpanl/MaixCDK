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
#include "csignal"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

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

using namespace maix;
using namespace maix::network;

#define PORT 8090
#define ENABLE_RTSP 0
#define ENABLE_PIPE 0


int record_number = 0;
std::string custom_dir = "videos";

std::string local_ip = "0.0.0.0";
std::string broadcast_ip = "255.255.255.255";

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

    const int UDP_PORT = 5005;
    const char* BROADCAST_IP = broadcast_ip.c_str();
    const size_t MAX_PACKET_SIZE = 1200;
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
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = inet_addr(BROADCAST_IP);

    uint32_t frame_id = 0;

    while (running) {
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
                perror("sendto failed");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(125));
    }

    close(sock);
}


void jpeg_worker_thread(DroppingQueue<image::Image*>& img_queue,
                        DroppingQueue<std::vector<uint8_t>>& jpeg_queue,
                        std::atomic<bool>& running)
{
    printf("*** START jpeg_worker ***\n");

    while (running) {
        image::Image* img = nullptr;
        if (!img_queue.try_pop(img)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        if (!img) {
            printf("jpeg_worker: got nullptr image, skipping\n");
            continue;
        }

        image::Image* jpg_img = img->to_format(image::FMT_JPEG);
        if (!jpg_img) {
            printf("jpeg_worker: to_format failed\n");
            delete img;
            continue;
        }

        uint8_t* jpeg_data = static_cast<uint8_t*>(jpg_img->data());
        size_t jpeg_size = jpg_img->data_size();

        std::vector<uint8_t> jpeg(jpeg_data, jpeg_data + jpeg_size);
        jpeg_queue.push(std::move(jpeg));

        delete jpg_img;
        delete img;

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}


#if 0
void jpeg_worker_thread(DroppingQueue<image::Image*>& img_queue,
                        DroppingQueue<std::vector<uint8_t>>& jpeg_queue,
                        std::atomic<bool>& running)

void jpeg_worker_thread(DroppingQueue<std::unique_ptr<maix::image::Image>>& img_queue,
                        DroppingQueue<std::vector<uint8_t>>& jpeg_queue,
                        std::atomic<bool>& running)
{
    printf("*** START jpeg_worker ***\n");

    while (running) {
#if 0
        image::Image* img = nullptr;
        if (!img_queue.try_pop(img)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        image::Image* jpg_img = img->to_format(image::FMT_JPEG);
        uint8_t* jpeg_data = (uint8_t*)jpg_img->data();
        size_t jpeg_size = jpg_img->data_size();

        std::vector<uint8_t> jpeg(jpeg_data, jpeg_data + jpeg_size);
        jpeg_queue.push(std::move(jpeg));

        delete jpg_img;
        delete img;
#endif

std::unique_ptr<image::Image> img;
if (!img_queue.try_pop(img)) {
    printf("jpeg_worker: no try_pop result\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    continue;
}
printf("jpeg_worker: got image\n");

if (!img) {
    printf("jpeg_worker: got nullptr image, skipping\n");
    continue;
}

auto jpg_img = std::unique_ptr<image::Image>(img->to_format(image::FMT_JPEG));
if (!jpg_img) {
    printf("jpeg_worker: to_format failed\n");
    continue;
}
//printf("jpeg_worker: to_format done\n");

uint8_t* jpeg_data = static_cast<uint8_t*>(jpg_img->data());
size_t jpeg_size = jpg_img->data_size();
std::cout << "Address of jpg_img->data(): " << static_cast<const void*>(jpg_img->data()) << std::endl;

std::vector<uint8_t> jpeg(jpeg_data, jpeg_data + jpeg_size);
jpeg_queue.push(std::move(jpeg));
//printf("jpeg_worker: jpeg pushed, size = %zu\n", jpeg_size);

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}
#endif

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
} priv;


#if ENABLE_PIPE
int pipe_fd = -1;

void save_video_to_file(const uint8_t *data, int size, int index) {
    if (pipe_fd < 0 || size <= 0 || data == nullptr) return;

    // 每帧都写入 SPS/PPS + 帧数据
    if (!g_sps_pps_buf.empty()) {
        write(pipe_fd, g_sps_pps_buf.data(), g_sps_pps_buf.size());
    }
    write(pipe_fd, data, size);
}

void save_audio_to_file(const uint8_t *data, int size, int index) {

}
#endif

#if 0
void save_video_to_file(const uint8_t *data, int size, int index) {
    char filename[64];
    snprintf(filename, sizeof(filename), "/root/%03d.bin", index);
    std::ofstream ofs(filename, std::ios::binary);
    if (ofs.is_open()) {
        ofs.write((const char *)data, size);
        ofs.close();
    }
}

void save_audio_to_file(const uint8_t *data, int size, int index) {
    write(ffmpeg_audio_fd, data, size);
    char filename[64];
    snprintf(filename, sizeof(filename), "/root/%03d_audio.bin", index);
    std::ofstream ofs(filename, std::ios::binary);
    if (ofs.is_open()) {
        ofs.write((const char *)data, size);
        ofs.close();
    }
}
#endif


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
        running = false;

        if (server_fd != -1) {
            close(server_fd);
            server_fd = -1;
        }

        {
            std::lock_guard<std::mutex> lock(client_mutex);
            for (auto& [fd, client] : clients) {
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
        std::queue<std::vector<char>> sendQueue;
        std::mutex queueMutex;
        std::condition_variable queueCond;
        std::atomic<bool> active;
        std::function<void(int, const std::vector<char>&)> onMessage;

        ClientHandler(int client_fd, std::function<void(int, const std::vector<char>&)> messageCallback)
            : fd(client_fd), active(true), onMessage(std::move(messageCallback)) {}

        void start() {
            thread = std::thread(&ClientHandler::run, this);
        }

        void stop() {
            active = false;
            queueCond.notify_all();
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

        void run() {
            while (active) {
                // Read
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

                // Send
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCond.wait_for(lock, std::chrono::milliseconds(10), [this]() { return !sendQueue.empty() || !active; });

                while (!sendQueue.empty()) {
                    auto& msg = sendQueue.front();
                    ssize_t sent = send(fd, msg.data(), msg.size(), 0);
                    if (sent < 0) {
                        std::cerr << "Send error on client: " << fd << std::endl;
                        active = false;
                        break;
                    }
                    sendQueue.pop();
                }
            }

            active = false;  // Mark as inactive for server cleanup
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

        while (running) {
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
                std::this_thread::sleep_for(std::chrono::milliseconds(100));  // avoid busy waiting
            }
        }

        std::cout << "Server stopped accepting new clients.\n";
    }
};




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


int _main(int argc, char* argv[])
{
    mmf_deinit_v2(true);

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

    // 启动 JPEG 转换线程
    std::thread jpeg_thread(jpeg_worker_thread, std::ref(img_queue), std::ref(jpeg_queue), std::ref(running));

    // 启动线程
    std::thread udp_thread(udp_broadcast_thread, std::ref(jpeg_queue), std::ref(running));


#if ENABLE_PIPE
    printf("Open PIPE\n");
    pipe_fd = open("/root/stream.h264", O_WRONLY);
    printf("Open PIPE DONE\n");
#endif

    TcpServer server;

/*
    server.setMessageCallback([](int client_fd, const std::vector<char>& data) {
        std::string msg(data.begin(), data.end());
        std::cout << "Received from " << client_fd << ": " << msg << std::endl;
    });

    server.start();
*/

    int cam_w = 1920;
    int cam_h = 1080;
    int cam2_w = 160;
    int cam2_h = 120;

    image::Format cam_fmt = image::Format::FMT_YVU420SP;
    int cam_fps = 30;
    int cam_buffer_num = 3;
    int cam_bitrate = 9 * 1000 * 1000;

    priv.audio_en = true;
    priv.video_stop_flag = false;

    priv.cam = new camera::Camera(cam_w, cam_h, cam_fmt, "", cam_fps, cam_buffer_num);
    priv.cam2 = priv.cam->add_channel(cam2_w, cam2_h, cam_fmt, cam_fps, cam_buffer_num);

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

    server.setMessageCallback([&](int client_fd, const std::vector<char>& data) {
        std::string msg(data.begin(), data.end());
        std::cout << "Received from " << client_fd << ": " << msg << std::endl;

        if (msg.rfind("set ", 0) == 0) {
            // set /root/videos/test.mp4
            std::string filename = msg.substr(4);
            recorderManager.setRecordFileName(filename);
            server.broadcastText("File name set to: " + filename + "\n");
        } else if (msg == "start") {
            priv.video_stop_flag = false;
            recorderManager.startRecord();
            server.broadcastText("Recording started.\n");
        } else if (msg == "stop") {
            priv.video_stop_flag = true;
            recorderManager.stopRecord();
            server.broadcastText("Recording stopped.\n");
        } else if (msg == "status") {
            std::string status = recorderManager.isRecording() ? "Recording\n" : "Stopped\n";
            server.broadcastText("Status: " + status + "\n");
        } else if (msg == "duration") {
            server.broadcastText("Duration: " + std::to_string(recorderManager.getRecordDuration()) + " ms\n");
        } else if (msg == "filesize") {
            server.broadcastText("File Size: " + std::to_string(recorderManager.getRecordFileSize()) + " bytes\n");
        } else {
            server.broadcastText("Unknown command.\n");
        }
    });

    server.start();

    // 创建目录
    std::string dir_path = "/root/" + custom_dir;
    mkdir(dir_path.c_str(), 0755);
    
    // 构造文件名
    std::string output_path = "/root/" + custom_dir + "/" + std::to_string(record_number) + ".mp4";
    printf("OutputPath = %s", output_path.c_str());
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

        // 从VI获取一帧数据
        int ch = priv.cam->get_channel();
        int res = _mmf_vi_frame_pop(ch, &frame, &f, 40);

        if (res != 0 || frame == nullptr) {
            printf("Failed to get frame, skipping...\n");
            time::sleep_ms(10);
            continue;
        }


//printf("*** push to venc\n");
        // 将帧数据推送至VENC
        mmf_venc_push2(1, frame);


//printf("*** from venc get data\n");
        // 从编码器通道1取出编码数据
        mmf_stream_t venc_stream = {0};
        if (0 == mmf_venc_pop(1, &venc_stream)) {
            if (venc_stream.count > 0) {
                found_venc_stream = true;
            }
        }

//printf("*** config pts sync\n");
        // 配置视频PTS与音频PTS同步
        if (priv.ffmpeg_packer && recorderManager.isOpened()) {
            double temp_us = priv.ffmpeg_packer->video_pts_to_us(priv.video_pts);
            priv.audio_pts = priv.ffmpeg_packer->audio_us_to_pts(temp_us);
        }

        // printf("*** 取出的编码数据: %d \n", venc_stream.count);


//printf("*** found venc stream?\n");
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

                            priv.last_read_cam_ms = 0;
                            priv.video_pts = 0;
                            priv.audio_pts = 0;
                        }
                        free(sps_pps);
                    }
                }
            }

//printf("*** push video to packeter\n");
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
#if ENABLE_PIPE
                    static int video_frame_index = 1;  // 全局或静态变量记录帧编号
                    save_video_to_file(data, data_size, video_frame_index++);
#endif
                    // 推入 ffmpeg 打包器
                    if (err::ERR_NONE != priv.ffmpeg_packer->push(data, data_size, priv.video_pts)) {
                        log::error("ffmpeg push failed!");
                    }
                }
            }


//printf("*** push audio to packeter\n");
            if (priv.audio_en && recorderManager.isOpened()) { 
                //printf("Audio is enabled and ffmpeg_packer is opened.\n"); // Check if audio is enabled and ffmpeg_packer is opened.

                int frame_size_per_second = priv.ffmpeg_packer->get_audio_frame_size_per_second();
                //printf("Frame size per second: %d\n", frame_size_per_second); // Output the frame size per second.

                uint64_t loop_ms = 0;
                int read_pcm_size = 0;

                if (priv.last_read_pcm_ms == 0) {
                    loop_ms = 30;
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

                read_pcm_size = (read_pcm_size + 1023) & ~1023;
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

#if ENABLE_PIPE
                        static int audio_frame_index = 1;  // 全局或静态变量记录帧编号
                        save_audio_to_file(pcm_data->data, pcm_data->data_len, audio_frame_index++);
#endif
                        // log::info("[AUDIO] pts:%d  pts %f s", priv.audio_pts, priv.ffmpeg_packer->audio_pts_to_us(priv.audio_pts) / 1000000);
                        if (err::ERR_NONE != priv.ffmpeg_packer->push(pcm_data->data, pcm_data->data_len, priv.audio_pts, true)) {
                            log::error("ffmpeg push failed!");
                            printf("ffmpeg push failed!\n"); // Debug ffmpeg push failure.
                        } else {
                           // printf("ffmpeg push succeeded.\n"); // Debug ffmpeg push success.
                        }
                    }
                    delete pcm_data;
                    //printf("PCM data deleted.\n"); // Debug PCM data deletion.
                } else {
                    printf("Failed to record PCM data.\n"); // Debug PCM data recording failure.
                }
            }

        }


//printf("*** release venc\n");
        // 释放VENC资源
        mmf_venc_free(1);

        // 将帧数据推送至VO
        //mmf_vo_frame_push2(0, 0, 2, frame);

//printf("*** release frame\n");
        // 释放帧数据
        _mmf_vi_frame_free(ch, &frame);
#if 0
        img_queue.push(img);
        priv.disp->show(*img);
#endif

        delete img;

//printf("*** read from cam2\n");
image::Image* img2 = priv.cam2->read();
//printf("*** disp show img2\n");
priv.disp->show(*img2);
//printf("*** img_queue push img2\n");
img_queue.push(img2);
        //image::Image *img2 = priv.cam2->read();    
        //priv.disp->show(*img2);
        //img_queue.push(std::move(img2));

//std::unique_ptr<image::Image> img2(priv.cam2->read());
//priv.disp->show(*img2);
//img_queue.push(std::move(img2)); // 自动管理内存


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
//printf("==================================\n");
        uint64_t curr_ms = time::ticks_ms();
        // log::info("loop use %lld ms\r\n", curr_ms - last_ms);
        last_ms = curr_ms;
    }

#if ENABLE_RTSP
    priv.rtsp->stop();
#endif

#if ENABLE_PIPE
    close(pipe_fd);
#endif

    running = false;
    jpeg_thread.join();
    udp_thread.join();

    server.stop();
    std::cout << "Server stopped.\n";

    printf("准备关闭视频流\n");

    priv.ffmpeg_packer->close();
    delete priv.encoder;
    delete priv.ffmpeg_packer;
#if ENABLE_RTSP
    delete priv.rtsp;
#endif
    delete priv.cam2;
    delete priv.cam;
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
