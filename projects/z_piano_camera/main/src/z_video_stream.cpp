#include "../include/z_video_stream.hpp"

#include "../../../../components/basic/include/maix_app.hpp"
#include "mmf_vi_helper.hpp"
#include "priv.hpp"

using namespace maix;

namespace z {
    VideoStream::VideoStream() {
        encode_ch_ = 3;
        camera_ch_ = -1;
        camera_ = nullptr;
        printf("==== VideoStream ====\n");
    }

    VideoStream::~VideoStream() {
        printf("~~~~ VideoStream ~~~~\n");

        stop();

        mmf_enc_jpg_deinit(encode_ch_);
    }

    void VideoStream::bind_camera(camera::Camera *camera) {
        if (!camera) {
            log::error("camera is null");
        }

        camera_ = camera;

        camera_ch_ = camera->get_channel();

        if (mmf_enc_jpg_init(encode_ch_, camera_->width(), camera_->height(), 19, 85) != 0) {
            printf("mmf_enc_jpg_init failed\n");
        } else {
            printf("mmf_enc_jpg_init success!\n");
        }


    }


    void VideoStream::broadcast_jpeg(uint8_t* jpg_data, int jpg_size) {
        static uint32_t frame_id = 0;
        const size_t MAX_PAYLOAD = 1400; // 每个UDP包数据部分最大值，安全起见小于MTU

        int total_packets = (jpg_size + MAX_PAYLOAD - 1) / MAX_PAYLOAD;

        for (int i = 0; i < total_packets; i++) {
            size_t offset = i * MAX_PAYLOAD;
            size_t chunk_size = std::min((size_t)jpg_size - offset, MAX_PAYLOAD);

            UdpJpegHeader header;
            header.frame_id = frame_id;
            header.total_packets = total_packets;
            header.packet_seq = i;
            header.payload_size = chunk_size;

            // 拼装包：头 + 数据
            std::vector<uint8_t> packet(sizeof(header) + chunk_size);
            memcpy(packet.data(), &header, sizeof(header));
            memcpy(packet.data() + sizeof(header), jpg_data + offset, chunk_size);

            // 通过UDP广播
            if (priv.udp_server) {
                priv.udp_server->broadcast(packet);
            }
        }

        frame_id++; // 每发一帧递增
    }


    // void VideoStream::run() {
    //     while (!app::need_exit()) {
    //         log::info("    VideoStream!");
    //
    //         void *frame = nullptr;
    //         mmf_frame_info_t f;
    //
    //         int res = _mmf_vi_frame_pop(camera_ch_, &frame, &f, 10);
    //         if (res != 0 || frame == nullptr) {
    //             printf("[thread_cam] Failed to get frame, skipping...\n");
    //             time::sleep_ms(5);
    //             continue;
    //         }
    //
    //         if (mmf_enc_jpg_push(encode_ch_, (uint8_t*)f.data, f.w, f.h, f.fmt) == 0) {
    //             uint8_t *jpg_data = nullptr;
    //             int jpg_size = 0;
    //             if (mmf_enc_jpg_pop(encode_ch_, &jpg_data, &jpg_size) == 0) {
    //                 printf("[thread_cam] JPEG size = %d bytes\n", jpg_size);
    //                 broadcast_jpeg(jpg_data, jpg_size);
    //                 mmf_enc_jpg_free(encode_ch_);
    //             }
    //         } else {
    //             printf("[thread_cam] mmf_enc_jpg_push failed\n");
    //         }
    //
    //         _mmf_vi_frame_free(camera_ch_, &frame);
    //
    //         std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    //     }
    // }

    // =====================================================
    // 打包成多个 UDP 包（不直接发）
    // =====================================================
    std::vector<std::vector<uint8_t>> VideoStream::prepare_packets(uint8_t* jpg_data, int jpg_size) {
        static uint32_t frame_id = 0;
        const size_t MAX_PAYLOAD = 1400;

        int total_packets = (jpg_size + MAX_PAYLOAD - 1) / MAX_PAYLOAD;
        std::vector<std::vector<uint8_t>> packets;

        for (int i = 0; i < total_packets; i++) {
            size_t offset = i * MAX_PAYLOAD;
            size_t chunk_size = std::min((size_t)jpg_size - offset, MAX_PAYLOAD);

            UdpJpegHeader header;
            header.frame_id = frame_id;
            header.total_packets = total_packets;
            header.packet_seq = i;
            header.payload_size = chunk_size;

            std::vector<uint8_t> packet(sizeof(header) + chunk_size);
            memcpy(packet.data(), &header, sizeof(header));
            memcpy(packet.data() + sizeof(header), jpg_data + offset, chunk_size);

            packets.push_back(std::move(packet));
        }

        frame_id++;
        return packets;
    }

    // =====================================================
    // 生产者：采集 → 编码 → 入队
    // =====================================================
    void VideoStream::run() {
        log::info("VideoStream wait udp 5s...\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(5000)); // 控制采集速率
        log::info("VideoStream wait udp 5s done!\n");

        while (!app::need_exit() && !stop_flag_) {
            void *frame = nullptr;
            mmf_frame_info_t f;

            int res = _mmf_vi_frame_pop(camera_ch_, &frame, &f, 50);
            if (res != 0 || frame == nullptr) {
                time::sleep_ms(50);
                continue;
            }

            if (mmf_enc_jpg_push(encode_ch_, (uint8_t*)f.data, f.w, f.h, f.fmt) == 0) {
                uint8_t *jpg_data = nullptr;
                int jpg_size = 0;
                if (mmf_enc_jpg_pop(encode_ch_, &jpg_data, &jpg_size) == 0) {
                    // log::info("JPEG Size = %d", jpg_size);

                    std::vector<uint8_t> data;
                    data.assign(jpg_data, jpg_data + jpg_size);

                    priv.udp_server->broadcast(data);

                    mmf_enc_jpg_free(encode_ch_);
                }
            }

            _mmf_vi_frame_free(camera_ch_, &frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 控制采集速率
        }
    }

    // =====================================================
    // 入队，带队列上限
    // =====================================================
    void VideoStream::enqueue_packets(std::vector<std::vector<uint8_t>>&& packets) {
        std::unique_lock<std::mutex> lock(mtx_);
        for (auto& pkt : packets) {
            cv_.wait(lock, [this] { return packet_queue_.size() < MAX_QUEUE_SIZE || stop_flag_; });
            if (stop_flag_) break;
            packet_queue_.push(std::move(pkt));
        }
        cv_.notify_all();
    }

    // =====================================================
    // 消费者线程：从队列取出并广播
    // =====================================================
    void VideoStream::consumer_thread() {
        while (!stop_flag_) {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !packet_queue_.empty() || stop_flag_; });

            if (stop_flag_) break;

            auto packet = std::move(packet_queue_.front());
            packet_queue_.pop();
            lock.unlock();
            cv_.notify_all();

            if (priv.udp_server) {
                log::info("udp broadcast = %d", packet.size());
                priv.udp_server->broadcast(packet);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 控制速率
        }
    }


    void VideoStream::start() {
        stop_flag_ = false;
        thread_ = std::thread(&VideoStream::run, this);
        consumer_ = std::thread(&VideoStream::consumer_thread, this);
    }

    void VideoStream::stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_flag_ = true;
        }
        cv_.notify_all();

        if (thread_.joinable()) thread_.join();
        if (consumer_.joinable()) consumer_.join();
    }


}