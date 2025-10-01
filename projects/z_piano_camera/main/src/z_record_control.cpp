#include "z_record_control.hpp"
#include <iostream>
#include "priv.hpp"
#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "maix_video.hpp"
#include "maix_ffmpeg.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cmath>

using namespace maix;

bool isKeyFrame(uint8_t* data, int size) {
    if (size < 5) return false;
    // 简化：假设 H.264, 取 NAL 单元类型
    uint8_t nal_unit_type = data[4] & 0x1F;
    return (nal_unit_type == 5); // IDR frame
}


void rename_after_delay(const std::string& path, int delay_seconds) {
    namespace fs = std::filesystem;

    // 在新线程里执行，避免阻塞
    std::thread([path, delay_seconds]() {
        try {
            fs::path p(path);
            auto filename = p.filename().string();

            // 判断文件名是否以 '.' 开头
            if (!filename.empty() && filename[0] == '.') {
                std::string new_filename = "@" + filename.substr(1);
                fs::path new_path = p.parent_path() / new_filename;

                std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));

                fs::rename(p, new_path);
                std::cout << "Renamed: " << p << " -> " << new_path << "\n";
            } else {
                std::cout << "Filename does not start with '.', no rename performed.\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }).detach(); // 分离线程，非阻塞
}

namespace z {

    RecordControl::RecordControl() {
        printf("==== RecordControl ====\n");
        m_running = true;
        m_state = State::WaitingSpsPps;
        m_pushThread = std::thread(&RecordControl::pushThreadLoop, this);
    }

    RecordControl::~RecordControl() {
        printf("~~~~ RecordControl BEGIN ~~~~\n");
        m_running = false;

        m_cv.notify_all();
        stop();

        if (m_pushThread.joinable()) {
            m_pushThread.join();
        }

        if (priv.ffmpeg_packer && priv.ffmpeg_packer->is_opened()) {
            priv.ffmpeg_packer->close();
        }
        printf("~~~~ RecordControl END ~~~~\n");
    }


    void RecordControl::setFileName(const std::string& filename) {
        m_filename = filename;
        if (priv.ffmpeg_packer) {
            priv.ffmpeg_packer->config2("path", filename.c_str());
        }
    }

    void RecordControl::start() {
        if (!priv.ffmpeg_packer) return;

        switch (m_state) {
            case State::Ready:
                m_state = State::Starting;
                // 绑定当前 session 的文件名
                m_sessionFilename = m_filename;
                // if (priv.ffmpeg_packer->open() == 0) {
                //     if (priv.audio_recorder) {
                //         priv.audio_recorder->reset();
                //     }
                //     resetTimer();
                //     // m_state = State::Recording;
                // }
                // resetTimer();
                log::info("RecordControl: waiting for first keyframe...");
                break;
            default:
                break;
        }
    }

    void RecordControl::stop() {
        if (!priv.ffmpeg_packer) return;

        switch (m_state) {
            case State::Recording:
                maix::log::info("当前状态是正在录制中，状态设置为Stopping");
                // 在 stop 时固化文件名，避免被下一次 start 覆盖
                m_pendingCloseFilename = m_sessionFilename;

                m_state = State::Stopping;
                // 不再立刻调用 close，由 pushThreadLoop 负责安全关闭
                // printf("notify all!!!\n");
                m_cv.notify_all();
                break;
            default:
                break;
        }
    }

    RecordControl::State RecordControl::state() const {
        return m_state;
    }

    double RecordControl::duration() const {
        if (m_state == State::Recording) {
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(now - m_startTime).count();
        } else if (m_state == State::Ready || m_state == State::Stopping) {
            return std::chrono::duration<double>(m_stopTime - m_startTime).count();
        }
        return 0.0;
    }

    std::string RecordControl::fileName() const {
        return m_filename;
    }

    void RecordControl::resetTimer() {
        m_startTime = std::chrono::steady_clock::now();
        m_stopTime = m_startTime;
    }

    void RecordControl::handleVideoFrame(uint8_t* data, int size,
                                         const std::vector<uint8_t>& sps_pps,
                                         uint64_t now_ms)
    {
        if (size <= 0) return;

        switch (m_state) {
            case State::WaitingSpsPps:
                if (!sps_pps.empty()) {
                    // 第一次真正收到 SPS/PPS
                    cached_sps_pps = sps_pps;
                    sps_pps_ready = true;

                    priv.ffmpeg_packer->config_sps_pps(
                        priv.encoder->get_sps_pps_ptr(cached_sps_pps),
                        priv.encoder->get_sps_pps_size(cached_sps_pps)
                    );
                    m_state = State::Ready;
                    log::info("SPS/PPS received and cached, switch to Ready");

                } else if (sps_pps_ready) {
                    // 没有新数据，但已经缓存过，就复用缓存
                    priv.ffmpeg_packer->config_sps_pps(
                        priv.encoder->get_sps_pps_ptr(cached_sps_pps),
                        priv.encoder->get_sps_pps_size(cached_sps_pps)
                    );
                    m_state = State::Ready;
                    log::info("Using cached SPS/PPS, switch to Ready");
                }

                return;

            case State::Starting:
                if (!cached_sps_pps.empty()) {
                    priv.ffmpeg_packer->config_sps_pps(
                        priv.encoder->get_sps_pps_ptr(cached_sps_pps),
                        priv.encoder->get_sps_pps_size(cached_sps_pps)
                    );
                }

                // 等到关键帧才真正开始
                if (isKeyFrame(data, size)) {
                    if (priv.ffmpeg_packer->open() == 0) {
                        if (priv.audio_recorder) priv.audio_recorder->reset();
                        priv.video_pts = 0;
                        priv.audio_pts = 0;
                        priv.last_read_cam_ms = now_ms;
                        priv.last_read_pcm_ms = now_ms;
                        resetTimer();
                        m_state = State::Recording;
                        log::info("Recording started with keyframe");

                        enqueueFrame(data, size, priv.video_pts, false);

                        // 推送这帧（关键帧作为第一帧）
                        // if (err::ERR_NONE != priv.ffmpeg_packer->push(data, size, priv.video_pts)) {
                        //     log::error("ffmpeg video push failed on first keyframe!");
                        // }
                    }
                } else {
                    log::debug("Skipping non-keyframe while waiting for first keyframe");
                }
                return;


            case State::Recording:
                    
                // 正常推送视频
                if (priv.last_read_cam_ms == 0) {
                    priv.video_pts = 0;
                    priv.last_read_cam_ms = now_ms;
                } else {
                    priv.video_pts += priv.ffmpeg_packer->video_us_to_pts(
                        (now_ms - priv.last_read_cam_ms) * 1000
                    );
                    priv.last_read_cam_ms = now_ms;
                }

                enqueueFrame(data, size, priv.video_pts, false);

                // if (err::ERR_NONE != priv.ffmpeg_packer->push(data, size, priv.video_pts)) {
                //     log::error("ffmpeg video push failed!");
                // }
                return;

            case State::Stopping:
                log::info("Recording stopping");
                return;

            default:
                return;
        }
    }

void RecordControl::handleAudioFrame(int sample_rate,
                                     int bytes_per_sample,
                                     uint64_t now_ms,
                                     uint64_t& last_audio_ms,
                                     double& sample_error_acc)
{
    if (m_state != State::Recording || !priv.ffmpeg_packer->is_opened()) return;

    double elapsed_ms = (double)(now_ms - last_audio_ms);
    last_audio_ms = now_ms;

    double ideal_samples = (sample_rate * elapsed_ms / 1000.0) + sample_error_acc;
    int samples_needed = (int)(ideal_samples + 0.5);
    sample_error_acc = ideal_samples - samples_needed;

    auto remain_frame_count = priv.audio_recorder->get_remaining_frames();
    int samples_to_read = std::min(samples_needed, (int)remain_frame_count);

    if (samples_to_read > 0) {
        int read_pcm_size = samples_to_read * bytes_per_sample;
        priv.audio_pts += samples_to_read;

        Bytes* pcm_data = priv.audio_recorder->record_bytes(read_pcm_size);
        if (pcm_data) {
            // ===== 新增部分：增益处理 + 耗时测量 =====
            auto t1 = std::chrono::high_resolution_clock::now();

            // 固定增益值（比如 +12 dB）
            double gain_db = 12.0;
            double gain = pow(10.0, gain_db / 20.0);

            if (bytes_per_sample == 2) { // int16_t PCM
                int16_t* samples = reinterpret_cast<int16_t*>(pcm_data->data);
                int sample_count = pcm_data->data_len / sizeof(int16_t);

                for (int i = 0; i < sample_count; i++) {
                    int32_t temp = static_cast<int32_t>(samples[i] * gain);
                    // 饱和裁剪 [-32768, 32767]
                    temp = std::max(-32768, std::min(32767, temp));
                    samples[i] = static_cast<int16_t>(temp);
                }
            } else if (bytes_per_sample == 4) { // float PCM
                float* samples = reinterpret_cast<float*>(pcm_data->data);
                int sample_count = pcm_data->data_len / sizeof(float);

                for (int i = 0; i < sample_count; i++) {
                    samples[i] = static_cast<float>(samples[i] * gain);
                }
            } else {
                printf(" !!! Unsupported bytes_per_sample=%d\n", bytes_per_sample);
            }

            auto t2 = std::chrono::high_resolution_clock::now();
            double elapsed_gain_ms =
                std::chrono::duration<double, std::milli>(t2 - t1).count();

            //printf("[AudioGain] Gain=%.2f dB, processed %d bytes, cost=%.3f ms\n",
            //       gain_db, pcm_data->data_len, elapsed_gain_ms);
            // ===== 新增部分结束 =====

            enqueueFrame(pcm_data->data, pcm_data->data_len, priv.audio_pts, true);
            delete pcm_data;
        } else {
            printf(" !!! Warning: record_bytes returned null\n");
        }
    } else {
        printf(" !!! No samples_to_read, skipping enqueueFrame\n");
    }
}

#if 0
void RecordControl::handleAudioFrame(int sample_rate,
                                     int bytes_per_sample,
                                     uint64_t now_ms,
                                     uint64_t& last_audio_ms,
                                     double& sample_error_acc)
{
    if (m_state != State::Recording || !priv.ffmpeg_packer->is_opened()) return;

    double elapsed_ms = (double)(now_ms - last_audio_ms);
    last_audio_ms = now_ms;

    double ideal_samples = (sample_rate * elapsed_ms / 1000.0) + sample_error_acc;
    int samples_needed = (int)(ideal_samples + 0.5);
    sample_error_acc = ideal_samples - samples_needed;

    auto remain_frame_count = priv.audio_recorder->get_remaining_frames();
    int samples_to_read = std::min(samples_needed, (int)remain_frame_count);

    // 🔍 详细调试日志
    // printf("[DEBUG][AudioFrame]\n");
    //     printf(" now_ms=%llu, last_audio_ms=%llu, elapsed_ms=%.3f ms\n",
    //            (unsigned long long)now_ms,
    //            (unsigned long long)last_audio_ms,
    //            elapsed_ms);
    // printf(" ideal_samples=%.3f, samples_needed=%d, sample_error_acc=%.6f\n",
    //        ideal_samples, samples_needed, sample_error_acc);
    // printf(" remain_frame_count=%d, samples_to_read=%d\n",
    //        (int)remain_frame_count, samples_to_read);
    // printf(" bytes_per_sample=%d, read_pcm_size=%d\n",
    //        bytes_per_sample, samples_to_read * bytes_per_sample);
    // printf(" priv.audio_pts=%llu\n",
    //        (unsigned long long)priv.audio_pts);

    if (samples_to_read > 0) {
        int read_pcm_size = samples_to_read * bytes_per_sample;
        priv.audio_pts += samples_to_read;

        Bytes* pcm_data = priv.audio_recorder->record_bytes(read_pcm_size);
        if (pcm_data) {
            // printf(" --> enqueueFrame: data_len=%d, pts=%llu\n",
            //        pcm_data->data_len, (unsigned long long)priv.audio_pts);
            enqueueFrame(pcm_data->data, pcm_data->data_len, priv.audio_pts, true);
            delete pcm_data;
        } else {
            printf(" !!! Warning: record_bytes returned null\n");
        }
    } else {
        printf(" !!! No samples_to_read, skipping enqueueFrame\n");
    }
}
#endif

    void RecordControl::enqueueFrame(const uint8_t* data, int size, uint64_t pts, bool is_audio) {
        if (size <= 0) return;

        // 如果正在停止，不再允许新帧入队
        if (m_state == State::Stopping) {
            // maix::log::info("当前状态是Stopping，不再接受入队");
            return;
        }

        auto pkt = std::make_unique<AVPacketData>();

        pkt->data.assign(data, data + size);
        pkt->pts = pts;
        pkt->is_audio = is_audio;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_avQueue.push(std::move(pkt));
        }
        m_cv.notify_one();
        // maix::log::info("  入队成功，notify_one");
    }

    void RecordControl::pushThreadLoop() {
        while (m_running && !app::need_exit()) {

            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]{
                return !m_avQueue.empty()
                    || m_state == State::Stopping
                    || !m_running
                    || app::need_exit();
            });

            // 先检查退出意图，避免再次睡回去
            if (!m_running || app::need_exit()) break;


            while (!m_avQueue.empty()) {
                std::unique_ptr<AVPacketData> pkt = std::move(m_avQueue.front());
                m_avQueue.pop();

                size_t queue_size = m_avQueue.size();

                lock.unlock();

                // 推送到 ffmpeg_packer
                if (pkt->is_audio) {
                    // maix::log::info("      实际推送音频");

                    if (err::ERR_NONE != priv.ffmpeg_packer->push(
                            pkt->data.data(), pkt->data.size(), pkt->pts, true)) {
                        log::error("ffmpeg audio push failed!");
                            }
                } else {
                    // maix::log::info("      实际推送视频");

                    if (err::ERR_NONE != priv.ffmpeg_packer->push(
                            pkt->data.data(), pkt->data.size(), pkt->pts)) {
                        log::error("ffmpeg video push failed!");
                            }
                }

                // std::cout << "\r[Buffer Status] Queue size: "
                //           << queue_size
                //           << " packets"
                //           << std::flush;

                lock.lock();
            }

            // === 队列清空 && 状态是 Stopping 时，安全关闭 ===
            if (m_state == State::Stopping && m_avQueue.empty()) {
                lock.unlock();
                // 注意：这里用 m_pendingCloseFilename，而不是 m_filename
                std::string to_close = m_pendingCloseFilename;

                printf("原始文件名: %s\n", to_close.c_str());
                priv.ffmpeg_packer->close();
                rename_after_delay(to_close, 10);
                printf("packer close.\n");
                m_stopTime = std::chrono::steady_clock::now();
                m_state = State::Ready;
                log::info("Recording stopped safely after queue flush");
                lock.lock();
            }
        }
    }


} // namespace z
