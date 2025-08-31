#include "maix_basic.hpp"
#include "maix_display.hpp"
#include "maix_camera.hpp"
#include "maix_image.hpp"
#include "maix_ffmpeg.hpp"
#include "maix_video.hpp"

#include "main.h"
#include "mmf_vi_helper.hpp"

#include <thread>

using namespace maix;

struct Priv {
    maix::display::Display *display;
    maix::camera::Camera *cam;
    maix::camera::Camera *cam2;
    video::Encoder *encoder;
};

Priv priv;

image::Format cam_fmt = image::Format::FMT_YVU420SP;
image::Format cam2_fmt = image::Format::FMT_YVU420SP;
int cam_w = 1920;
int cam_h = 1080;
int cam2_w = 640;
int cam2_h = 360;
int cam_fps = 30;
int cam_buffer_num = 3;
int cam_bitrate = 9 * 1000 * 1000;

// 线程1：处理 ch0
void thread_cam0(int ch0, int jpg_ch)
{

    while(!app::need_exit())
    {
        void *frame = nullptr;
        mmf_frame_info_t f;

        int res = _mmf_vi_frame_pop(ch0, &frame, &f, 10);
        if (res != 0 || frame == nullptr) {
            printf("[thread_cam0] Failed to get frame, skipping...\n");
            time::sleep_ms(5);
            continue;
        }

        mmf_vo_frame_push2(0, 0, 1, frame);


        int res2 = mmf_venc_push2(jpg_ch, frame);
        mmf_stream_t venc_stream = {0};
        if (0 == mmf_venc_pop(jpg_ch, &venc_stream)) {
            if (venc_stream.count > 0) {
                log::info("Encode success count=%d", venc_stream.count);

                for (int i = 0; i < venc_stream.count; i ++) {
                    uint8_t *p = (uint8_t *)venc_stream.data[i];
                    int len   = venc_stream.data_size[i];

/*
                    printf("[%d] stream.data:%p stream.len:%d\n", i, p, len);

                    // 打印前 16 个字节（避免越界）
                    int dump_len = len < 16 ? len : 16;
                    printf("    Hex: ");
                    for (int j = 0; j < dump_len; j++) {
                        printf("%02X ", p[j]);
                    }
                    printf("\n");
*/
                }
            } else {
                log::info("Encode count = 0");
            }
        } else {
                log::info("Encode fail!");
        }

        mmf_venc_free(jpg_ch);


/*
        if (mmf_enc_jpg_push(jpg_ch, (uint8_t*)f.data, f.w, f.h, f.fmt) == 0) {
            uint8_t *jpg_data = nullptr;
            int jpg_size = 0;
            if (mmf_enc_jpg_pop(jpg_ch, &jpg_data, &jpg_size) == 0) {
                printf("[thread_cam0] JPEG size = %d bytes\n", jpg_size);
                mmf_enc_jpg_free(jpg_ch);
            }
        } else {
            printf("[thread_cam0] mmf_enc_jpg_push failed\n");
        }
*/

        _mmf_vi_frame_free(ch0, &frame);
        time::sleep_ms(100);
    }
}

// 线程2：处理 ch1
void thread_cam1(int ch1, int jpg_ch1)
{
    while(!app::need_exit())
    {
        void *frame1 = nullptr;
        mmf_frame_info_t f1;

        int res1 = _mmf_vi_frame_pop(ch1, &frame1, &f1, 10);
        if (res1 != 0 || frame1 == nullptr) {
            printf("[thread_cam1] Failed to get frame, skipping...\n");
            time::sleep_ms(5);
            continue;
        }

        if (mmf_enc_jpg_push(jpg_ch1, (uint8_t*)f1.data, f1.w, f1.h, f1.fmt) == 0) {
            uint8_t *jpg_data = nullptr;
            int jpg_size = 0;
            if (mmf_enc_jpg_pop(jpg_ch1, &jpg_data, &jpg_size) == 0) {
                printf("[thread_cam1] JPEG 3 size = %d bytes\n", jpg_size);
                mmf_enc_jpg_free(jpg_ch1);
            }
        } else {
            printf("[thread_cam1] mmf_enc_jpg_push 3 failed\n");
        }

        _mmf_vi_frame_free(ch1, &frame1);
        time::sleep_ms(1000);
    }
}

int _main(int argc, char* argv[])
{
    log::info("Program start");

    priv.display = new maix::display::Display();
    priv.cam = new maix::camera::Camera(cam_w, cam_h, cam_fmt, "", cam_fps, cam_buffer_num);

    printf("================================\n");
    priv.cam2 = priv.cam->add_channel(cam2_w, cam2_h, cam2_fmt, cam_fps, cam_buffer_num);
    printf("================================\n");

    int ch0 = priv.cam->get_channel();
    int ch1 = priv.cam2->get_channel();
    log::info("ch0: %d, ch1: %d\n", ch0, ch1);

    int jpg_ch = 1;
    int jpg_ch1 = 3;

/*
    if (mmf_enc_jpg_init(jpg_ch, cam_w, cam_h, 19, 85) != 0) {
        printf("mmf_enc_jpg_init failed\n");
    } else {
        printf("mmf_enc_jpg_init success!\n");
    }
 */

    priv.encoder = new video::Encoder("", cam_w, cam_h, image::Format::FMT_YVU420SP, video::VideoType::VIDEO_H265, cam_fps, 50, cam_bitrate, 1000, false, true, 1);


    if (mmf_enc_jpg_init(jpg_ch1, cam2_w, cam2_h, 19, 85) != 0) {
        printf("mmf_enc_jpg_init 3 failed\n");
    } else {
        printf("mmf_enc_jpg_init 3 success!\n");
    }

    // 启动两个线程
    std::thread t0(thread_cam0, ch0, jpg_ch);
    std::thread t1(thread_cam1, ch1, jpg_ch1);

    t0.detach();
    t1.detach();

    while(!app::need_exit())
    {
        time::sleep_ms(1000);
    }

    mmf_enc_jpg_deinit(jpg_ch);
    mmf_enc_jpg_deinit(jpg_ch1);

    delete priv.display;
    delete priv.cam;
    return 0;
}

int main(int argc, char* argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
