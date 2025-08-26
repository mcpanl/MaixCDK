#pragma once
#include "maix_camera.hpp"
#include "maix_video.hpp"
#include "sophgo_middleware.hpp"

namespace z {
    using EncodeHandler = std::function<void(const std::vector<uint8_t>&)>;

    class Encoder {
    public:
        Encoder(maix::camera::Camera* cam);
        ~Encoder();

        // 允许外部注册多个消费者
        void addHandler(EncodeHandler handler) {
            _handlers.push_back(std::move(handler));
        }

        const std::vector<uint8_t>& getSpsPps() const { return sps_pps_buf; }

        // 获取指针
        uint8_t* get_sps_pps_ptr(const std::vector<uint8_t>& buf) {
            return buf.empty() ? nullptr : const_cast<uint8_t*>(buf.data());
        }

        // 获取长度
        int get_sps_pps_size(const std::vector<uint8_t>& buf) {
            return static_cast<int>(buf.size());
        }

        // 从内部 pop 出一帧，返回包装后的结果
        bool getFrame(void *frame, mmf_stream_t& venc_stream) {
            mmf_venc_push2(1, frame);

            if (0 == mmf_venc_pop(1, &venc_stream)) {
                // 只有在 sps_pps_buf 为空时才提取 SPS/PPS
                if (sps_pps_buf.empty() && venc_stream.count > 1) {
                    int sps_pps_size = venc_stream.data_size[0] + venc_stream.data_size[1];
                    sps_pps_buf.resize(sps_pps_size);

                    memcpy(sps_pps_buf.data(), venc_stream.data[0], venc_stream.data_size[0]);
                    memcpy(sps_pps_buf.data() + venc_stream.data_size[0],
                           venc_stream.data[1], venc_stream.data_size[1]);

                    printf("***** CONFIG SPS/PPS extracted, size=%d *****\n", sps_pps_size);
                }

                return true;
            }

            return false;
        }

        void freeFrame() {
            mmf_venc_free(1);
        }

        uint64_t bitrate() const { return _bitrate; }

    private:
        maix::camera::Camera* _camera;
        maix::video::Encoder *encoder;
        uint64_t _bitrate = 0;
        std::vector<EncodeHandler> _handlers;
        std::vector<uint8_t> sps_pps_buf;
    };

}