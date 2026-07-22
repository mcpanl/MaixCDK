#include "maix_basic.hpp"
#include "main.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "z_display.hpp"
#include "z_camera.hpp"
#include "z_image.hpp"
#include "z_video.hpp"

extern "C" {
#include "zonhor_mmf.h"
}

using namespace maix;

const char *fb_dev = "/dev/fb0";

/* Parse Annex-B H.264 for a coarse picture type from NAL headers. */
static const char *h264_picture_type(const uint8_t *data, size_t size)
{
    if (!data || size < 5)
        return "-";
    const char *pic = "-";
    for (size_t i = 0; i + 4 < size; ++i) {
        size_t sc = 0;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            sc = 3;
        else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 &&
                 data[i + 2] == 0 && data[i + 3] == 1)
            sc = 4;
        if (!sc)
            continue;
        uint8_t nal = data[i + sc] & 0x1f;
        if (nal == 5)
            return "IDR";
        if (nal == 1) {
            /* slice_type is in the first ue after first_mb; coarse: peek low bits */
            if (i + sc + 1 < size) {
                /* Many encoders put slice_type early; 0/3/5~I, 1/6~P, 2/7~B */
                uint8_t b = data[i + sc + 1];
                /* Not a full exp-Golomb parse — prefer VENC pack log for accuracy. */
                (void)b;
            }
            pic = "SLICE";
        } else if (nal == 7)
            pic = (pic[0] == '-') ? "SPS" : pic;
        else if (nal == 8)
            pic = (pic[0] == '-' || !strcmp(pic, "SPS")) ? "PPS" : pic;
        i += sc;
    }
    return pic;
}

static void dump_encoded_frame(int idx, video::Frame *f)
{
    if (!f) {
        log::info("Frame#%d <null>", idx);
        return;
    }
    if (!f->is_valid()) {
        log::info("Frame#%d invalid (empty)", idx);
        return;
    }
    const char *nal = h264_picture_type(f->data(), f->size());
    log::info("Frame#%d valid size=%zu pts=%llu dts=%llu nal~%s data=%p",
              idx, f->size(),
              (unsigned long long)f->get_pts(),
              (unsigned long long)f->get_dts(),
              nal, (void *)f->data());
}

int _main(int argc, char *argv[])
{
    int max_frames = 0; /* 0 = run until app::need_exit() */
    bool encode_only = false;
    bool force_wrong_size = false;
    const char *out_path = "/tmp/demo2.mp4";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--encode-only"))
            encode_only = true;
        else if (!strcmp(argv[i], "--force-wrong-size"))
            force_wrong_size = true;
        else if (!strncmp(argv[i], "--out=", 6))
            out_path = argv[i] + 6;
        else if (argv[i][0] >= '0' && argv[i][0] <= '9')
            max_frames = atoi(argv[i]);
    }

    log::info("Program start (max_frames=%d encode_only=%d force_wrong_size=%d out=%s)",
              max_frames, (int)encode_only, (int)force_wrong_size, out_path);

    display::Display *disp = nullptr;
    if (!encode_only)
        disp = new display::Display(172, 320, image::FMT_RGB888, fb_dev);
    camera::Camera *cam = new camera::Camera(1080, 1920, image::FMT_RGB888);

    log::info("camera opened: %dx%d (user coords)", cam->width(), cam->height());

    /* Encoder must use SUB_VENC valid/logical size, not Camera main RGB size. */
    z_camera_output_desc_t sub_desc;
    memset(&sub_desc, 0, sizeof(sub_desc));
    int enc_w = 576;
    int enc_h = 1024;
    if (ZONHOR_MMF_GetOutputDesc(Z_CAMERA_OUTPUT_SUB_VENC, &sub_desc) == CVI_SUCCESS) {
        enc_w = (int)(sub_desc.extent.valid_width ? sub_desc.extent.valid_width
                                                  : sub_desc.extent.logical_width);
        enc_h = (int)(sub_desc.extent.valid_height ? sub_desc.extent.valid_height
                                                   : sub_desc.extent.logical_height);
        log::info("SUB_VENC desc: logical=%ux%u buffer=%ux%u valid=(%u,%u %ux%u)",
                  sub_desc.extent.logical_width, sub_desc.extent.logical_height,
                  sub_desc.extent.buffer_width, sub_desc.extent.buffer_height,
                  sub_desc.extent.valid_x, sub_desc.extent.valid_y,
                  sub_desc.extent.valid_width, sub_desc.extent.valid_height);
    } else {
        log::warn("GetOutputDesc(SUB_VENC) failed, fallback Encoder %dx%d", enc_w, enc_h);
    }

    int ctor_w = enc_w;
    int ctor_h = enc_h;
    if (force_wrong_size) {
        ctor_w = 1080;
        ctor_h = 1920;
        log::warn("force wrong Encoder ctor size %dx%d (bind should recreate to SUB_VENC)",
                  ctor_w, ctor_h);
    }

    video::Encoder *enc = new video::Encoder(out_path, ctor_w, ctor_h,
                                             image::FMT_YVU420SP, video::VIDEO_H264_CBR,
                                             30, 30, 2000 * 1000);

    if (enc->bind_camera(cam) != err::ERR_NONE) {
        log::error("encoder bind_camera failed");
        delete enc;
        delete cam;
        delete disp;
        return -1;
    }

    log::info("Encoder bound: %dx%d -> %s", enc->width(), enc->height(), out_path);

    int ok = 0;
    int loops = 0;
    while (!app::need_exit()) {
        if (!encode_only) {
            image::Image *cam_img = cam->read();
            if (cam_img) {
                disp->show(*cam_img);
                delete cam_img;
            }
        }

        video::Frame *f = enc->encode();
        dump_encoded_frame(loops, f);
        if (f && f->is_valid())
            ++ok;
        delete f;

        ++loops;
        if (max_frames > 0 && loops >= max_frames)
            break;

        time::sleep_ms(33);
    }

    log::info("encode ok: %d / loops=%d", ok, loops);
    log::info("Program exit");

    delete enc;
    delete cam;
    delete disp;

    return 0;
}

int main(int argc, char *argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
