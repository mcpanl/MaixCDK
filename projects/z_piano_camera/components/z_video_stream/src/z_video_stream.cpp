/**
 * z_video_stream.cpp —— JPEG 帧采集、编码与 UDP 分片广播实现
 *
 * ⚠️  本文件属于「闭源」部分，不随项目源码发布。
 *      分发时仅提供预编译的 prebuilt/libz_video_stream.so。
 *
 * 依赖注入：
 *   通过 VideoStream::set_context() 传入 VideoStreamContext::broadcast 回调，
 *   彻底解耦 priv.hpp / z_udp_server.hpp 等项目私有头文件，
 *   使本文件可独立编译为 .so，无需项目业务模块的头文件。
 */
#include "z_video_stream.hpp"

#include "maix_app.hpp"
#include "mmf_vi_helper.hpp"

#include <cstring>
#include <chrono>

using namespace maix;

namespace z {

VideoStream::VideoStream()
    : encode_ch_(3), camera_ch_(-1), camera_(nullptr)
{
    printf("==== VideoStream ====\n");
}

VideoStream::~VideoStream() {
    printf("~~~~ VideoStream ~~~~\n");

    stop();

    mmf_enc_jpg_deinit(encode_ch_);
}

void VideoStream::set_context(const VideoStreamContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx_mutex_);
    ctx_ = ctx;
}

void VideoStream::bind_camera(camera::Camera *camera) {
    if (!camera) {
        log::error("camera is null");
        return;
    }

    camera_     = camera;
    camera_ch_  = camera->get_channel();

    if (mmf_enc_jpg_init(encode_ch_, camera_->width(), camera_->height(), 19, 85) != 0) {
        printf("[VideoStream] mmf_enc_jpg_init failed\n");
    } else {
        printf("[VideoStream] mmf_enc_jpg_init success! ch=%d w=%d h=%d\n",
               encode_ch_, camera_->width(), camera_->height());
    }
}

// =====================================================
// 广播单帧 JPEG（拆包后逐包通过回调发出）
// =====================================================
void VideoStream::broadcast_jpeg(uint8_t* jpg_data, int jpg_size) {
    auto packets = prepare_packets(jpg_data, jpg_size);

    std::lock_guard<std::mutex> lock(ctx_mutex_);
    if (!ctx_.broadcast) return;

    for (auto& pkt : packets) {
        ctx_.broadcast(pkt);
    }
}

// =====================================================
// 打包成多个 UDP 分片（不直接发）
// =====================================================
std::vector<std::vector<uint8_t>> VideoStream::prepare_packets(uint8_t* jpg_data, int jpg_size) {
    static uint32_t frame_id = 0;
    const size_t MAX_PAYLOAD = 1400;

    int total_packets = (jpg_size + MAX_PAYLOAD - 1) / MAX_PAYLOAD;
    std::vector<std::vector<uint8_t>> packets;
    packets.reserve(total_packets);

    for (int i = 0; i < total_packets; i++) {
        size_t offset     = i * MAX_PAYLOAD;
        size_t chunk_size = std::min((size_t)jpg_size - offset, MAX_PAYLOAD);

        UdpJpegHeader header;
        header.frame_id      = frame_id;
        header.total_packets = static_cast<uint16_t>(total_packets);
        header.packet_seq    = static_cast<uint16_t>(i);
        header.payload_size  = static_cast<uint16_t>(chunk_size);

        std::vector<uint8_t> packet(sizeof(header) + chunk_size);
        memcpy(packet.data(),               &header,              sizeof(header));
        memcpy(packet.data() + sizeof(header), jpg_data + offset, chunk_size);

        packets.push_back(std::move(packet));
    }

    frame_id++;
    return packets;
}

// =====================================================
// 生产者线程：采集帧 → JPEG 编码 → 广播
// =====================================================
void VideoStream::run() {
    while (!app::need_exit() && !stop_flag_) {
        void* frame = nullptr;
        mmf_frame_info_t f;

        int res = _mmf_vi_frame_pop(camera_ch_, &frame, &f, 50);
        if (res != 0 || frame == nullptr) {
            time::sleep_ms(50);
            continue;
        }

        if (mmf_enc_jpg_push(encode_ch_, (uint8_t*)f.data, f.w, f.h, f.fmt) == 0) {
            uint8_t* jpg_data = nullptr;
            int      jpg_size = 0;

            if (mmf_enc_jpg_pop(encode_ch_, &jpg_data, &jpg_size) == 0) {
                std::vector<uint8_t> data(jpg_data, jpg_data + jpg_size);

                // 通过回调广播（线程安全）
                {
                    std::lock_guard<std::mutex> lock(ctx_mutex_);
                    if (ctx_.broadcast) {
                        ctx_.broadcast(data);
                    }
                }

                mmf_enc_jpg_free(encode_ch_);
            }
        }

        _mmf_vi_frame_free(camera_ch_, &frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

// =====================================================
// 入队（带队列上限保护）
// =====================================================
void VideoStream::enqueue_packets(std::vector<std::vector<uint8_t>>&& packets) {
    std::unique_lock<std::mutex> lock(mtx_);
    for (auto& pkt : packets) {
        cv_.wait(lock, [this] {
            return packet_queue_.size() < MAX_QUEUE_SIZE || stop_flag_;
        });
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
        cv_.wait(lock, [this] {
            return !packet_queue_.empty() || stop_flag_;
        });

        if (stop_flag_) break;

        auto packet = std::move(packet_queue_.front());
        packet_queue_.pop();
        lock.unlock();
        cv_.notify_all();

        {
            std::lock_guard<std::mutex> ctx_lock(ctx_mutex_);
            if (ctx_.broadcast) {
                ctx_.broadcast(packet);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void VideoStream::start() {
    stop_flag_ = false;
    thread_   = std::thread(&VideoStream::run, this);
    consumer_ = std::thread(&VideoStream::consumer_thread, this);
}

void VideoStream::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_flag_ = true;
    }
    cv_.notify_all();

    if (thread_.joinable())   thread_.join();
    if (consumer_.joinable()) consumer_.join();
}

} // namespace z
