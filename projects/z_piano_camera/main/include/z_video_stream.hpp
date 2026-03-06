#pragma once

#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>

struct UdpJpegHeader {
    uint32_t frame_id;      // 帧号
    uint16_t total_packets; // 本帧总分片数
    uint16_t packet_seq;    // 当前分片序号 (0-based)
    uint16_t payload_size;  // 有效负载大小
};

namespace z {

    // ======== 依赖注入上下文（解耦 priv/project 私有头文件）========
    //
    // 调用方（main.cpp）在 bind_camera() 之前通过 set_context() 注入：
    //   - broadcast : 将一包数据广播出去（通常转发给 UdpServer::broadcast）
    //
    // 这样 z_video_stream.cpp 不再依赖 priv.hpp，可单独编译为独立 .so。
    struct VideoStreamContext {
        // 广播一包 JPEG 分片数据
        std::function<void(const std::vector<uint8_t>&)> broadcast;
    };

    class VideoStream {
        public:
        VideoStream();
        ~VideoStream();

        // 注入运行时依赖（建议在 start() 之前调用，线程安全）
        void set_context(const VideoStreamContext& ctx);

        void bind_camera(maix::camera::Camera *camera);
        void start();
        void stop();

    private:
        maix::camera::Camera *camera_;

        int encode_ch_;
        int camera_ch_;
        void broadcast_jpeg(uint8_t* jpg_data, int jpg_size);
        void run();
        void consumer_thread();
        void enqueue_packets(std::vector<std::vector<uint8_t>>&& packets);

        std::vector<std::vector<uint8_t>> prepare_packets(uint8_t* jpg_data, int jpg_size);

        std::thread thread_;        // 生产者线程
        std::thread consumer_;      // 消费者线程

        std::mutex mtx_;
        std::condition_variable cv_;
        std::queue<std::vector<uint8_t>> packet_queue_;

        bool stop_flag_ = false;
        const size_t MAX_QUEUE_SIZE = 200;

        VideoStreamContext ctx_;    // 注入的依赖
        std::mutex ctx_mutex_;      // 保护 ctx_ 读写
    };
}
