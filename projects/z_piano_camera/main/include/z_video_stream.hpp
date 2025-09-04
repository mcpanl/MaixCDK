#pragma once

#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

struct UdpJpegHeader {
    uint32_t frame_id;      // 帧号
    uint16_t total_packets; // 本帧总分片数
    uint16_t packet_seq;    // 当前分片序号 (0-based)
    uint16_t payload_size;  // 有效负载大小
};

namespace z {
    class VideoStream {
        public:
        VideoStream();
        ~VideoStream();
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

        // 原始广播函数被修改，只拼装包，不直接发
        std::vector<std::vector<uint8_t>> prepare_packets(uint8_t* jpg_data, int jpg_size);

        std::thread thread_;        // 生产者线程
        std::thread consumer_;      // 消费者线程

        std::mutex mtx_;
        std::condition_variable cv_;
        std::queue<std::vector<uint8_t>> packet_queue_;

        bool stop_flag_ = false;
        const size_t MAX_QUEUE_SIZE = 200; // 队列上限（可调）
    };
}
