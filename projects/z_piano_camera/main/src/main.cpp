#include "main.h"
#include "priv.hpp"

#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "maix_video.hpp"
#include "maix_ffmpeg.hpp"

#include "z_udp_server.hpp"
#include "z_tcp_server.hpp"
#include "z_display.hpp"
#include "z_encoder.hpp"
#include "z_record_control.hpp"

#include "mmf_vi_helper.hpp"

#include <deque>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <sstream>
#include <iomanip>


using namespace maix;

Priv priv;


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


void handlerTcpMessage(int fd, const std::vector<char>& data)
{
    std::string msg(data.begin(), data.end());
    std::cout << "[TCP RECV from " << fd << "] " << msg << std::endl;
}

image::Format cam_fmt = image::Format::FMT_YVU420SP;
image::Format cam2_fmt = image::Format::FMT_RGB888;
int cam_w = 1920;
int cam_h = 1080;
int cam2_w = 320;
int cam2_h = 180;
int cam_fps = 30;
int cam_buffer_num = 5;
int cam_bitrate = 9 * 1000 * 1000;

const int target_frame_interval_ms = 39 - 1;
static std::vector<uint8_t> g_sps_pps_buf;


uint64_t last_start_time = 0;       // 上次开始时间
const int record_duration_ms = 60 * 1000; // 录制 10 秒
const int start_interval_ms = 50 * 1000 + 10 * 1000; // 每 5 秒触发一次

int _main(int argc, char* argv[])
{
    mmf_deinit_v2(true);

    uint64_t t = time::time_s();

    log::info("Program start at %d", t);

    priv.display = new z::Display();
    priv.display->showLogo("assets/logo.png");

    priv.cam = new camera::Camera(cam_w, cam_h, cam_fmt, "", cam_fps, cam_buffer_num);
    priv.cam2 = priv.cam->add_channel(cam2_w, cam2_h, cam2_fmt, cam_fps, cam_buffer_num);

    priv.cam -> skip_frames(30);

    priv.udp_server = new z::UdpServer();

    priv.udp_server->start();

    priv.tcp_server = new z::TcpServer();
    priv.tcp_server->setMessageCallback([](int fd, const std::vector<char>& data) {
        handlerTcpMessage(fd, data);
    });

    priv.tcp_server->start();

    priv.encoder = new z::Encoder(priv.cam);

    uint32_t i = 0;


    priv.audio_recorder = new audio::Recorder();
    err::check_null_raise(priv.audio_recorder, "audio recorder init failed!");


    // priv._encoder = new video::Encoder("", cam_w, cam_h, image::Format::FMT_YVU420SP, video::VideoType::VIDEO_H264, 24, 50, priv.encoder->bitrate(), 1000, false, true, 1);

    priv.ffmpeg_packer = new ffmpeg::FFmpegPacker();
    err::check_null_raise(priv.ffmpeg_packer, "ffmpeg packer init failed");
    err::check_bool_raise(!priv.ffmpeg_packer->config("has_video", true), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_codec_id", AV_CODEC_ID_H264), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_width", cam_w), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_height", cam_h), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_bitrate", priv.encoder->bitrate()), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_fps", cam_fps), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("video_pixel_format", AV_PIX_FMT_NV21), "ffmpeg packer config failed!");

    err::check_bool_raise(!priv.ffmpeg_packer->config("has_audio", true), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_sample_rate", 48000), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_channels", 1), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_bitrate", 128000), "ffmpeg packer config failed!");
    err::check_bool_raise(!priv.ffmpeg_packer->config("audio_format", AV_SAMPLE_FMT_S16), "ffmpeg packer config failed!");


    const int sample_rate = 48000;
    const int bytes_per_sample = 2 * 1; // 16bit * 单声道
    double sample_error_acc = 0.0;      // 累积误差（小数采样点）
    int64_t audio_pts = 0;              // 音频 PTS（单位：采样点）

    uint64_t last_audio_ms = time::ticks_ms(); // 上一次推送音频的时间

    // --- FPS 统计 ---
    std::deque<long long> frame_times;
    const int fps_window_ms = 2000;  // 平滑窗口 2s

    priv.ffmpeg_packer->config2("path", "/root/20250825.mp4");

    priv.video_pts = 0;
    priv.audio_pts = 0;
    priv.last_read_cam_ms = 0;
    priv.last_read_pcm_ms = 0;

    uint64_t total_audio_samples_sent = 0;

    priv.recordControl = new z::RecordControl();

    while(!app::need_exit())
    {
        uint64_t now_ms = time::ticks_ms();
        auto loop_start = time::time_ms();  // 记录循环开始时间


        // 判断是否需要启动新录制
        if ((!priv.recordControl || priv.recordControl->state() == z::RecordControl::State::Ready) &&
            (now_ms - last_start_time >= start_interval_ms)) {

            ensure_dir("/root/record");
            std::string filename = "/root/record/" + timestamp_str() + ".mp4";

            priv.recordControl->setFileName(filename);
            priv.recordControl->start();
            last_start_time = now_ms;

            log::info("开始录制: %s", filename.c_str());
            }

        // 判断是否需要停止录制
        if (priv.recordControl && priv.recordControl->state() == z::RecordControl::State::Recording) {
            double elapsed = priv.recordControl->duration();
            if (elapsed >= record_duration_ms / 1000.0) {
                priv.recordControl->stop();
                log::info("录制结束，持续: %.2f 秒", elapsed);
            }
        }

        bool found_venc_stream = false;
        void *frame = NULL;
        mmf_frame_info_t f;

        int ch = priv.cam->get_channel();
        int res = _mmf_vi_frame_pop(ch, &frame, &f, 10);

        if (res != 0 || frame == nullptr) {
            printf("Failed to get frame, skipping...\n");
            time::sleep_ms(5);
            continue;
        }

/*
        if (priv.recordControl->state() == z::RecordControl::State::Recording) {
            std::cout << "正在录制，时长: " << priv.recordControl->duration() << " 秒\n";
        } else {
            std::cout << "未在录制\n";
        }
*/

        mmf_vo_frame_push2(0, 0, 1, frame);
        // mmf_vo_frame_push2(0, 0, 2, frame);

        mmf_stream_t venc_stream = {0};

        bool isSuccess = priv.encoder->getFrame(frame, venc_stream);

        if (isSuccess) {
            const auto& sps_pps_buf = priv.encoder->getSpsPps();

            uint8_t* data = nullptr;
            int data_size = 0;
            if (venc_stream.count == 1) {
                data = venc_stream.data[0];
                data_size = venc_stream.data_size[0];
            } else if (venc_stream.count > 1) {
                data = venc_stream.data[2]; // 跳过 SPS/PPS
                data_size = venc_stream.data_size[2];
            }

            // 🔑 把关键数据交给状态机来处理
            priv.recordControl->handleVideoFrame(
                data,
                data_size,
                sps_pps_buf,
                time::ticks_ms()
            );

            priv.recordControl->handleAudioFrame(
                sample_rate,
                bytes_per_sample,
                last_audio_ms,
                sample_error_acc
            );
        }



#if 0
        if (isSuccess) {
            const auto& sps_pps_buf = priv.encoder->getSpsPps();

            uint8_t *data = NULL;
            int data_size = 0;
            if (venc_stream.count == 1) {
                data = venc_stream.data[0];
                data_size = venc_stream.data_size[0];
            } else if (venc_stream.count > 1) {
                data = venc_stream.data[2]; // 跳过 SPS/PPS，只取 IDR/P 帧
                data_size = venc_stream.data_size[2];
            }

            // 配置音画同步
            if (priv.ffmpeg_packer && priv.ffmpeg_packer->is_opened()) {
                double temp_us = priv.ffmpeg_packer->video_pts_to_us(priv.video_pts);
                priv.audio_pts = priv.ffmpeg_packer->audio_us_to_pts(temp_us);
            }

            // H264录制（整体+画面）
            if (data_size > 0 && !sps_pps_buf.empty()) {
                if(!priv.ffmpeg_packer->is_opened()) {
                    priv.ffmpeg_packer->config_sps_pps(
                        priv.encoder->get_sps_pps_ptr(sps_pps_buf),
                        priv.encoder->get_sps_pps_size(sps_pps_buf)
                    );
                    while (0 != priv.ffmpeg_packer->open() && !app::need_exit()) {
                        time::sleep_ms(500);
                        log::info("Can't open ffmpeg, retry again...");
                    }

                    if (priv.audio_recorder) {
                        priv.audio_recorder->reset();
                    }

                    total_audio_samples_sent = 0;

                    priv.video_pts = 0;
                    priv.audio_pts = 0;
                    priv.last_read_cam_ms = 0;
                    priv.last_read_pcm_ms = 0;
                }

                // PTS 计算
                if (priv.last_read_cam_ms == 0) {
                    priv.video_pts = 0;
                    priv.last_read_cam_ms = time::ticks_ms();
                } else {
                    priv.video_pts += priv.ffmpeg_packer->video_us_to_pts((time::ticks_ms() - priv.last_read_cam_ms) * 1000);
                    priv.last_read_cam_ms = time::ticks_ms();
                }

                if (err::ERR_NONE != priv.ffmpeg_packer->push(data, data_size, priv.video_pts)) {
                    log::error("ffmpeg push failed!");
                } else {
                    // log::info("ffmpeg push success");
                }
            }

            // H264录制（音频）
            if (data_size > 0 && !sps_pps_buf.empty() && priv.ffmpeg_packer->is_opened()) {
                uint64_t _curr_ms = time::ticks_ms();
                double elapsed_ms = (double)(_curr_ms - last_audio_ms);
                last_audio_ms = _curr_ms;

                // 理想采样数（包含误差修正）
                double ideal_samples = (sample_rate * elapsed_ms / 1000.0) + sample_error_acc;
                int samples_needed = (int)(ideal_samples + 0.5);
                sample_error_acc = ideal_samples - samples_needed;

                // 获取剩余可读取的帧数
                auto remain_frame_count = priv.audio_recorder->get_remaining_frames();

                // 限制实际读取的采样数不超过可用帧数
                int samples_to_read = std::min(samples_needed, (int)remain_frame_count);

                // 如果需要帧对齐，比如要求读取数是N的倍数，可以这样做：
                // int frame_align = 4; // 假设设备要求每次读取4帧对齐
                // samples_to_read = (samples_to_read / frame_align) * frame_align;

                if (samples_to_read > 0) {
                    int read_pcm_size = samples_to_read * bytes_per_sample;
                    audio_pts += samples_to_read;
                    // printf("@ pcm_read=%d, audio_pts=%d\n", read_pcm_size, audio_pts);

                    Bytes *pcm_data = priv.audio_recorder->record_bytes(read_pcm_size);
                    if (pcm_data) {
                        // printf("@@ pcm_len=%d\n", pcm_data->data_len);

                        if (err::ERR_NONE != priv.ffmpeg_packer->push(pcm_data->data, pcm_data->data_len, priv.audio_pts, true)) {
                            log::error("ffmpeg push failed!");
                            printf("ffmpeg push failed!\n"); // Debug ffmpeg push failure.
                        }

                        delete pcm_data;
                    }
                }
            }

            // TCP广播画面
            if (data_size > 0 && !sps_pps_buf.empty()) {
                // 拼接数据： [SPS+PPS] + [当前帧]
                std::vector<uint8_t> full_frame;
                full_frame.reserve(sps_pps_buf.size() + data_size);
                full_frame.insert(full_frame.end(), sps_pps_buf.begin(), sps_pps_buf.end());
                full_frame.insert(full_frame.end(), data, data + data_size);

                // 前4字节写帧长度（大端序）
                uint32_t frame_size = static_cast<uint32_t>(full_frame.size());
                std::vector<uint8_t> packet(4);
                packet[0] = (frame_size >> 24) & 0xFF;
                packet[1] = (frame_size >> 16) & 0xFF;
                packet[2] = (frame_size >> 8) & 0xFF;
                packet[3] = (frame_size) & 0xFF;

                // 拼成最终发送包
                std::vector<char> send_buf;
                send_buf.reserve(4 + frame_size);
                send_buf.insert(send_buf.end(), packet.begin(), packet.end());
                send_buf.insert(send_buf.end(), full_frame.begin(), full_frame.end());

                // 广播出去
                priv.tcp_server->broadcastBinary(send_buf);
            }
        }
#endif

        priv.encoder->freeFrame();

        i++;
        // 将 uint32_t 转换为字节数组（使用大端字节序，网络字节序）
        // std::vector<uint8_t> data(4);
        // data[0] = (i >> 24) & 0xFF;  // 最高有效字节
        // data[1] = (i >> 16) & 0xFF;
        // data[2] = (i >> 8) & 0xFF;
        // data[3] = i & 0xFF;          // 最低有效字节

        // priv.udp_server->broadcast(data);
        // priv.tcp_server->broadcastBinary(std::vector<char>(data.begin(), data.end()));
        // std::cout << "广播数字: " << (int)i << std::endl;

        // log::info("%d", time::time_s());

        // --- FPS 统计 ---
        auto now = time::time_ms();
        frame_times.push_back(now);

        // 删除窗口外的时间戳
        while (!frame_times.empty() && now - frame_times.front() > fps_window_ms) {
            frame_times.pop_front();
        }

        // 计算平滑 FPS
        if (frame_times.size() >= 2) {
            long long span = frame_times.back() - frame_times.front();
            double smooth_fps = (frame_times.size() - 1) * 1000.0 / span;

            priv.display -> setFps(smooth_fps);
            // priv.display -> runFrame();
            // printf("FPS (smoothed): %.2f\n", smooth_fps);
        }



        _mmf_vi_frame_free(ch, &frame);

        // --- 智能延时计算 ---
        auto loop_end = time::time_ms();
        int elapsed = (int)(loop_end - loop_start);  // 本轮循环耗时
        int sleep_time = target_frame_interval_ms - elapsed;
        if (sleep_time > 0) {
            // printf(": Sleep %d\n", sleep_time);
            time::sleep_ms(sleep_time);
        } else {
            // printf(": Not Sleep !\n");
        }
    }
    log::info("Program exit");
    priv.ffmpeg_packer->close();
    priv.udp_server->stop();
    priv.tcp_server->stop();

    delete priv.encoder;
    delete priv.tcp_server;
    delete priv.udp_server;
    delete priv.display;
    delete priv.recordControl;

    return 0;
}

int main(int argc, char* argv[])
{
    sys::register_default_signal_handle();

    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}


