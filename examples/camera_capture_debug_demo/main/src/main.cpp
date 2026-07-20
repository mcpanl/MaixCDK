/**
 * Minimal Zonhor camera isolation demo.
 *
 * Isolates camera::Camera::read() from display / resize / HUD.
 * Captures frames for at least 1 second, saves the last RGB frame to disk.
 * Also dumps DISPLAY endpoint once for A/B against MAIN_RGB.
 */

#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "z_image.hpp"
#include "main.h"

extern "C" {
#include "zonhor_mmf.h"
#include "cvi_vpss.h"
#include "cvi_sys.h"
}

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

using namespace maix;

/* Match fb_display_camera_demo so we exercise the same MAIN_RGB path. */
static constexpr int kCamWidth = 720;
static constexpr int kCamHeight = 720;
static constexpr int kCaptureMs = 1000;
static constexpr int kSkipFrames = 5;
static constexpr const char *kOutPath = "/root/camera_capture_debug/last.jpg";
static constexpr const char *kDispOutPath = "/root/camera_capture_debug/display.jpg";
static constexpr const char *kG0CpuOutPath = "/root/camera_capture_debug/g0_cpu_rgb.jpg";

static void print_usage(const char *prog)
{
    printf("Usage: %s [width] [height] [duration_ms] [out_path]\n", prog);
    printf("  Defaults: %dx%d, %dms, %s\n",
           kCamWidth, kCamHeight, kCaptureMs, kOutPath);
    printf("  Captures RGB888 via cam.read() for >= duration_ms, saves last frame.\n");
    printf("  Also dumps DISPLAY endpoint once to %s\n", kDispOutPath);
}

static void log_frame_geometry(const char *tag, const VIDEO_FRAME_S *vf)
{
    if (!vf)
        return;
    log::info("%s fmt=%d wh=%ux%u stride=%u/%u/%u len=%u/%u/%u "
              "offset L/R/T/B=%d/%d/%d/%d\n",
              tag,
              (int)vf->enPixelFormat,
              vf->u32Width, vf->u32Height,
              vf->u32Stride[0], vf->u32Stride[1], vf->u32Stride[2],
              vf->u32Length[0], vf->u32Length[1], vf->u32Length[2],
              (int)vf->s16OffsetLeft, (int)vf->s16OffsetRight,
              (int)vf->s16OffsetTop, (int)vf->s16OffsetBottom);
}

static image::Image *copy_rgb888_from_frame(VIDEO_FRAME_S *vf, const z_frame_extent_t *extent)
{
    if (!vf || !vf->u64PhyAddr[0] || vf->u32Length[0] == 0)
        return nullptr;

    log_frame_geometry("dump_rgb", vf);

    uint8_t *src = (uint8_t *)CVI_SYS_MmapCache(vf->u64PhyAddr[0], vf->u32Length[0]);
    if (!src)
        return nullptr;
    CVI_SYS_IonInvalidateCache(vf->u64PhyAddr[0], src, vf->u32Length[0]);

    uint32_t vx = extent ? extent->valid_x : 0;
    uint32_t vy = extent ? extent->valid_y : 0;
    uint32_t vw = extent && extent->valid_width ? extent->valid_width : vf->u32Width;
    uint32_t vh = extent && extent->valid_height ? extent->valid_height : vf->u32Height;
    uint32_t stride = vf->u32Stride[0];

    log::info("dump_frame valid=(%u,%u %ux%u) extent_buf=%ux%u\n",
              vx, vy, vw, vh,
              extent ? extent->buffer_width : vf->u32Width,
              extent ? extent->buffer_height : vf->u32Height);

    image::Image *img = new image::Image((int)vw, (int)vh, image::FMT_RGB888);
    uint8_t *dst = (uint8_t *)img->data();
    uint32_t row_bytes = vw * 3u;
    for (uint32_t y = 0; y < vh; ++y) {
        memcpy(dst + (size_t)y * row_bytes,
               src + (size_t)(vy + y) * stride + (size_t)vx * 3u,
               row_bytes);
    }

    CVI_SYS_Munmap(src, vf->u32Length[0]);
    return img;
}

static void dump_display_endpoint(const char *path)
{
    VIDEO_FRAME_INFO_S frame;
    z_camera_output_desc_t desc;
    memset(&frame, 0, sizeof(frame));
    memset(&desc, 0, sizeof(desc));

    if (ZONHOR_MMF_GetOutputDesc(Z_CAMERA_OUTPUT_DISPLAY, &desc) != CVI_SUCCESS) {
        log::warn("GetOutputDesc(DISPLAY) failed\n");
        return;
    }
    if (ZONHOR_MMF_PreviewGetFrame(&frame, 2000) != CVI_SUCCESS) {
        log::warn("PreviewGetFrame failed\n");
        return;
    }

    image::Image *img = copy_rgb888_from_frame(&frame.stVFrame, &desc.extent);
    ZONHOR_MMF_PreviewReleaseFrame(&frame);
    if (!img) {
        log::warn("display frame copy failed\n");
        return;
    }
    err::Err e = img->save(path, 95);
    if (e != err::ERR_NONE)
        log::error("save display %s failed: %d\n", path, (int)e);
    else
        log::info("saved DISPLAY frame to %s (%dx%d)\n",
                  path, img->width(), img->height());
    delete img;
}

/**
 * DIAGNOSTIC ONLY (not a product fix): dump Group0 rotated NV21 and CPU-convert
 * to RGB to prove whether the YUV path is healthy while HW RGB CSC is broken.
 */
static void dump_g0_nv21_cpu_rgb(const char *path)
{
    VIDEO_FRAME_INFO_S frame;
    memset(&frame, 0, sizeof(frame));

    CVI_S32 ret = CVI_VPSS_GetChnFrame(0, 0, &frame, 2000);
    if (ret != CVI_SUCCESS) {
        log::warn("G0 GetChnFrame failed: 0x%x\n", ret);
        return;
    }

    VIDEO_FRAME_S *vf = &frame.stVFrame;
    log_frame_geometry("G0-NV21", vf);

    size_t map_size = (size_t)vf->u32Length[0] + (size_t)vf->u32Length[1] +
                      (size_t)vf->u32Length[2];
    if (map_size == 0 || !vf->u64PhyAddr[0]) {
        CVI_VPSS_ReleaseChnFrame(0, 0, &frame);
        return;
    }

    uint8_t *base = (uint8_t *)CVI_SYS_MmapCache(vf->u64PhyAddr[0], map_size);
    if (!base) {
        CVI_VPSS_ReleaseChnFrame(0, 0, &frame);
        return;
    }
    CVI_SYS_IonInvalidateCache(vf->u64PhyAddr[0], base, map_size);

    /* Build contiguous NV21 for OpenCV (Y plane then VU). */
    int w = (int)vf->u32Width;
    int h = (int)vf->u32Height;
    int y_stride = (int)vf->u32Stride[0];
    uint8_t *y_src = base;
    uint8_t *uv_src = base + vf->u32Length[0];
    if (vf->u32Length[1] == 0 && vf->pu8VirAddr[1]) {
        /* some drivers pack UV after Y with same map */
        uv_src = base + (size_t)y_stride * (size_t)h;
    }

    std::vector<uint8_t> nv21((size_t)w * (size_t)h * 3 / 2);
    for (int y = 0; y < h; ++y)
        memcpy(nv21.data() + (size_t)y * w, y_src + (size_t)y * y_stride, (size_t)w);
    int uv_h = h / 2;
    int uv_stride = vf->u32Stride[1] ? (int)vf->u32Stride[1] : y_stride;
    for (int y = 0; y < uv_h; ++y)
        memcpy(nv21.data() + (size_t)w * h + (size_t)y * w,
               uv_src + (size_t)y * uv_stride, (size_t)w);

    CVI_SYS_Munmap(base, map_size);
    CVI_VPSS_ReleaseChnFrame(0, 0, &frame);

    cv::Mat yuv(h + h / 2, w, CV_8UC1, nv21.data());
    cv::Mat bgr;
    cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV21);

    /* Crop letterbox only for portrait rotated buffers (e.g. 1088x1920). */
    int crop_x = 0;
    int crop_w = w;
    if (h > w && w > 1080) {
        crop_x = (w - 1080) / 2;
        crop_w = 1080;
    }
    cv::Mat cropped = bgr(cv::Rect(crop_x, 0, crop_w, h)).clone();

    image::Image img(cropped.cols, cropped.rows, image::FMT_BGR888,
                     cropped.data, cropped.cols * cropped.rows * 3, false);
    err::Err e = img.save(path, 95);
    if (e != err::ERR_NONE)
        log::error("save G0 CPU-RGB %s failed: %d\n", path, (int)e);
    else
        log::info("saved G0 CPU-RGB (diagnostic) to %s (%dx%d)\n",
                  path, img.width(), img.height());
}

int _main(int argc, char *argv[])
{
    int width = kCamWidth;
    int height = kCamHeight;
    int duration_ms = kCaptureMs;
    const char *out_path = kOutPath;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    if (argc > 1)
        width = atoi(argv[1]);
    if (argc > 2)
        height = atoi(argv[2]);
    if (argc > 3)
        duration_ms = atoi(argv[3]);
    if (argc > 4)
        out_path = argv[4];

    if (width <= 0 || height <= 0 || duration_ms <= 0) {
        log::error("invalid args: width=%d height=%d duration_ms=%d\n",
                   width, height, duration_ms);
        print_usage(argv[0]);
        return -1;
    }

    log::info("camera_capture_debug_demo: %dx%d RGB888, capture >= %dms, out=%s\n",
              width, height, duration_ms, out_path);

    camera::Camera cam(width, height, image::FMT_RGB888);
    log::info("camera opened: %dx%d format=%d\n",
              cam.width(), cam.height(), (int)cam.format());

    cam.skip_frames(kSkipFrames);
    log::info("skipped %d startup frames\n", kSkipFrames);

    /* A/B: hardware display RGB path vs Camera::read MAIN_RGB path. */
    dump_display_endpoint(kDispOutPath);
    /* DIAGNOSTIC ONLY: CPU NV21->RGB from Group0 to prove YUV path health. */
    dump_g0_nv21_cpu_rgb(kG0CpuOutPath);

    image::Image *last = nullptr;
    int frames = 0;
    int fail = 0;
    uint64_t t0 = time::ticks_ms();

    while (!app::need_exit()) {
        image::Image *img = cam.read();
        if (!img) {
            ++fail;
            log::error("cam.read() failed (fail=%d)\n", fail);
            if (fail >= 10)
                break;
            continue;
        }

        ++frames;
        fail = 0;
        log::info("frame %d: %dx%d format=%d size=%d\n",
                  frames, img->width(), img->height(),
                  (int)img->format(), img->data_size());

        if (last)
            delete last;
        last = img;

        if ((int)(time::ticks_ms() - t0) >= duration_ms)
            break;
    }

    uint64_t elapsed = time::ticks_ms() - t0;
    log::info("capture done: frames=%d elapsed=%llums\n",
              frames, (unsigned long long)elapsed);

    if (!last) {
        log::error("no frame captured, nothing to save\n");
        return -1;
    }

    err::Err e = last->save(out_path, 95);
    if (e != err::ERR_NONE) {
        log::error("save %s failed: %d\n", out_path, (int)e);
        delete last;
        return -1;
    }

    log::info("saved last frame to %s (%dx%d)\n",
              out_path, last->width(), last->height());
    delete last;
    return 0;
}

int main(int argc, char *argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
