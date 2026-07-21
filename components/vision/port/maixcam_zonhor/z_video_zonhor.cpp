/**
 * Zonhor maix::video port — zonhor_mmf VENC backend.
 *
 * Step3/4 scope (2026-07):
 * - Encoder path="" + H264 CBR: ZONHOR_MMF_Venc* bare stream
 * Step5 (2026-07):
 * - bind_camera() + encode(NULL): G3 SUB_VENC bind path, GetStream only
 * - Other Encoder/Decoder/Video/VideoRecorder paths: explicit NOT_IMPL
 */

#include "maix_basic.hpp"
#include "maix_image.hpp"
#include "maix_time.hpp"
#include "z_video.hpp"
#include "maix_camera.hpp"
#include "z_pipeline.hpp"

extern "C" {
#include "zonhor_mmf.h"
#include "cvi_venc.h"
}

#include <cstdlib>
#include <cstring>

#define ZONHOR_ENCODER_VENC_CHN  1
#define ZONHOR_VENC_SEND_TIMEOUT_MS  2000
#define ZONHOR_VENC_GET_TIMEOUT_MS   200

namespace maix::video {

#if CONFIG_BUILD_WITH_MAIXPY
    maix::image::Image *Video::NoneImage = new maix::image::Image();
    maix::image::Image *Encoder::NoneImage = new maix::image::Image();
    maix::Bytes *Encoder::NoneBytes = new maix::Bytes();
#else
    maix::image::Image *Video::NoneImage = NULL;
    maix::image::Image *Encoder::NoneImage = NULL;
    maix::Bytes *Encoder::NoneBytes = NULL;
#endif

    typedef struct {
        bool venc_created;
        bool bound;
        VENC_CHN chn;
    } zonhor_enc_priv_t;

    static video::Frame *_pull_venc_frame(zonhor_enc_priv_t *priv, int time_base,
                                          bool *encode_started, uint64_t *start_encode_ms)
    {
        uint64_t curr_ms = time::ticks_ms();
        if (!*encode_started) {
            *encode_started = true;
            *start_encode_ms = curr_ms;
        }
        uint64_t diff_ms = curr_ms - *start_encode_ms;
        uint64_t pts = diff_ms * 1000 / (uint64_t)time_base;
        uint64_t dts = pts;

        ZONHOR_MMF_VENC_STREAM_S mmf_stream;
        memset(&mmf_stream, 0, sizeof(mmf_stream));
        CVI_S32 ret = ZONHOR_MMF_VencGetStream(priv->chn, &mmf_stream,
                                               ZONHOR_VENC_GET_TIMEOUT_MS);
        if (ret != CVI_SUCCESS) {
            if (ret != CVI_ERR_VENC_BUSY)
                log::error("ZONHOR_MMF_VencGetStream failed: 0x%x\r\n", ret);
            return new video::Frame();
        }

        int stream_size = 0;
        uint8_t *stream_buffer = _merge_venc_stream(&mmf_stream, &stream_size);
        ZONHOR_MMF_VencReleaseStream(priv->chn, &mmf_stream);

        return new video::Frame(stream_buffer, stream_size, pts, dts, 0, true, false);
    }

    static void _drain_bound_camera(camera::Camera *cam, image::Image **capture_out)
    {
        if (!cam)
            return;

        image::Image *img = cam->read();
        if (!img)
            return;

        if (capture_out) {
            if (*capture_out && (*capture_out)->data()) {
                delete *capture_out;
                *capture_out = NULL;
            }
            *capture_out = new image::Image(img->width(), img->height(), img->format(),
                                            (uint8_t *)img->data(), img->data_size(), false);
        }
        delete img;
    }

    static void zonhor_video_not_impl(const char *what)
    {
        err::check_raise(err::ERR_NOT_IMPL, what);
    }

    static PAYLOAD_TYPE_E _video_type_to_payload(VideoType video_type)
    {
        switch (video_type) {
        case VIDEO_H264:
        case VIDEO_H264_CBR:
        case VIDEO_H264_VBR:
            return PT_H264;
        case VIDEO_H265:
        case VIDEO_H265_CBR:
        case VIDEO_H265_VBR:
            return PT_H265;
        default:
            err::check_raise(err::ERR_RUNTIME, "Unsupported video type!");
        }
        return PT_H264;
    }

    static ZONHOR_MMF_VENC_CFG_S _make_venc_cfg(VENC_CHN chn, int width, int height,
                                                int framerate, int gop, int bitrate,
                                                PAYLOAD_TYPE_E payload)
    {
        ZONHOR_MMF_VENC_CFG_S cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.chn = chn;
        cfg.payload = payload;
        cfg.width = (CVI_U32)width;
        cfg.height = (CVI_U32)height;
        cfg.fps = (CVI_U32)(framerate > 0 ? framerate : 30);
        cfg.gop = (CVI_U32)(gop > 0 ? gop : 30);
        cfg.bitrate_kbps = (CVI_U32)(bitrate > 0 ? (bitrate / 1000) : 2000);
        cfg.cbr = CVI_TRUE;
        return cfg;
    }

    static uint8_t *_merge_venc_stream(const ZONHOR_MMF_VENC_STREAM_S *mmf_stream, int *out_size)
    {
        int total = 0;
        uint8_t *buf = NULL;

        if (!mmf_stream || !out_size)
            return NULL;
        *out_size = 0;

        for (CVI_U32 i = 0; i < mmf_stream->stream.u32PackCount; ++i) {
            total += (int)mmf_stream->stream.pstPack[i].u32Len;
        }
        if (total <= 0)
            return NULL;

        buf = (uint8_t *)malloc((size_t)total);
        if (!buf)
            return NULL;

        int off = 0;
        for (CVI_U32 i = 0; i < mmf_stream->stream.u32PackCount; ++i) {
            const VENC_PACK_S *pk = &mmf_stream->stream.pstPack[i];
            const CVI_U8 *p = pk->pu8Addr ? (pk->pu8Addr + pk->u32Offset) : NULL;
            if (p && pk->u32Len > 0) {
                memcpy(buf + off, p, pk->u32Len);
                off += (int)pk->u32Len;
            }
        }
        *out_size = off;
        return buf;
    }

    static zonhor_enc_priv_t *_enc_priv(void *param)
    {
        return (zonhor_enc_priv_t *)param;
    }

    /* ── Encoder (Step4: bare H264 stream) ─────────────────────────────── */

    Encoder::Encoder(std::string path, int width, int height, image::Format format,
                     VideoType type, int framerate, int gop, int bitrate, int time_base,
                     bool capture, bool block)
    {
        _path = path;
        _width = width;
        _height = height;
        _format = format;
        _type = type;
        _framerate = framerate;
        _gop = gop;
        _bitrate = bitrate;
        _time_base = time_base > 0 ? time_base : 1000;
        _need_capture = capture;
        _capture_image = NULL;
        _camera = NULL;
        _bind_camera = false;
        _start_encode_ms = 0;
        _encode_started = false;
        _block = block;
        _param = NULL;

        err::check_bool_raise(format == image::Format::FMT_YVU420SP,
                              "Encoder only support FMT_YVU420SP format!");

        if (_path.size() != 0) {
            zonhor_video_not_impl("Encoder file/mp4/flv path not migrated on Zonhor yet");
        }

        PAYLOAD_TYPE_E payload = _video_type_to_payload(type);
        if (payload != PT_H264) {
            zonhor_video_not_impl("Encoder H265 not supported on Zonhor yet");
        }

        zonhor_enc_priv_t *priv = (zonhor_enc_priv_t *)calloc(1, sizeof(zonhor_enc_priv_t));
        err::check_null_raise(priv, "calloc failed");
        priv->chn = ZONHOR_ENCODER_VENC_CHN;

        ZONHOR_MMF_VENC_CFG_S cfg = _make_venc_cfg(priv->chn, _width, _height,
                                                   _framerate, _gop, _bitrate, payload);
        CVI_S32 ret = ZONHOR_MMF_VencCreate(&cfg);
        if (ret != CVI_SUCCESS) {
            free(priv);
            err::check_raise(err::ERR_RUNTIME, "ZONHOR_MMF_VencCreate failed");
        }

        priv->venc_created = true;
        _param = priv;
    }

    Encoder::~Encoder()
    {
        zonhor_enc_priv_t *priv = _enc_priv(_param);
        if (priv) {
            if (priv->bound)
                ZONHOR_MMF_VencUnbindInput(priv->chn, Z_CAMERA_OUTPUT_SUB_VENC);
            if (priv->venc_created)
                ZONHOR_MMF_VencDestroy(priv->chn);
            free(priv);
            _param = NULL;
        }

        if (_capture_image && _capture_image->data()) {
            delete _capture_image;
            _capture_image = nullptr;
        }
    }

    err::Err Encoder::bind_camera(camera::Camera *camera)
    {
        if (!camera)
            return err::ERR_ARGS;

        if (!ZONHOR_MMF_IsInited()) {
            err::check_raise(err::ERR_RUNTIME,
                             "Encoder bind_camera: zonhor graph not initialized");
            return err::ERR_RUNTIME;
        }

        zonhor_enc_priv_t *priv = _enc_priv(_param);
        if (!priv || !priv->venc_created) {
            err::check_raise(err::ERR_RUNTIME, "Encoder VENC not created");
            return err::ERR_RUNTIME;
        }

        if (priv->bound) {
            log::warn("Encoder already bound, skip re-bind\r\n");
            _camera = camera;
            _bind_camera = true;
            return err::ERR_NONE;
        }

        CVI_S32 ret = ZONHOR_MMF_EnableOutput(Z_CAMERA_OUTPUT_SUB_VENC);
        if (ret != CVI_SUCCESS) {
            log::error("ZONHOR_MMF_EnableOutput(SUB_VENC) failed: 0x%x\r\n", ret);
            return err::ERR_RUNTIME;
        }

        ret = ZONHOR_MMF_VencBindInput(priv->chn, Z_CAMERA_OUTPUT_SUB_VENC);
        if (ret != CVI_SUCCESS) {
            log::error("ZONHOR_MMF_VencBindInput failed: 0x%x\r\n", ret);
            return err::ERR_RUNTIME;
        }

        priv->bound = true;
        _camera = camera;
        _bind_camera = true;
        log::info("Encoder bind_camera: SUB_VENC -> VENC(%d) ok\r\n", priv->chn);
        return err::ERR_NONE;
    }

    video::Frame *Encoder::encode(image::Image *img, Bytes *pcm)
    {
        (void)pcm;

        zonhor_enc_priv_t *priv = _enc_priv(_param);
        if (!priv || !priv->venc_created)
            return new video::Frame();

        /* Bind path: encode(NULL) pulls bitstream only (Step5). */
        if (!img || !img->data()) {
            if (!priv->bound || !_bind_camera) {
                log::warn("Encoder encode(NULL): bind_camera() required\r\n");
                return new video::Frame();
            }

            if (priv->bound)
                _drain_bound_camera(_camera, _need_capture ? &_capture_image : NULL);

            return _pull_venc_frame(priv, _time_base, &_encode_started, &_start_encode_ms);
        }

        if (priv->bound) {
            log::warn("Encoder bound to camera; ignore input image, use bind path\r\n");
            return encode(Encoder::NoneImage, Encoder::NoneBytes);
        }

        if (img->format() != image::Format::FMT_YVU420SP) {
            log::error("Encoder only accepts FMT_YVU420SP input\r\n");
            return new video::Frame();
        }

        uint64_t curr_ms = time::ticks_ms();
        if (!_encode_started) {
            _encode_started = true;
            _start_encode_ms = curr_ms;
        }
        uint64_t diff_ms = curr_ms - _start_encode_ms;
        uint64_t pts = get_pts(diff_ms);
        uint64_t dts = get_dts(diff_ms);

        int img_w = img->width();
        int img_h = img->height();
        if (img_w != _width || img_h != _height) {
            log::warn("image size mismatch, re-create VENC %dx%d -> %dx%d\r\n",
                      _width, _height, img_w, img_h);
            ZONHOR_MMF_VencDestroy(priv->chn);
            priv->venc_created = false;

            ZONHOR_MMF_VENC_CFG_S cfg = _make_venc_cfg(priv->chn, img_w, img_h,
                                                       _framerate, _gop, _bitrate, PT_H264);
            if (ZONHOR_MMF_VencCreate(&cfg) != CVI_SUCCESS) {
                err::check_raise(err::ERR_RUNTIME, "ZONHOR_MMF_VencCreate failed on resize");
            }
            priv->venc_created = true;
            _width = img_w;
            _height = img_h;
        }

        if (_need_capture) {
            if (_capture_image && _capture_image->data()) {
                delete _capture_image;
                _capture_image = NULL;
            }
            _capture_image = new image::Image(img_w, img_h, img->format(),
                                              (uint8_t *)img->data(), img->data_size(), false);
        }

        CVI_S32 ret = ZONHOR_MMF_VencSendNv21UserData(priv->chn,
                                                      (const CVI_U8 *)img->data(),
                                                      (CVI_U32)img_w, (CVI_U32)img_h,
                                                      ZONHOR_VENC_SEND_TIMEOUT_MS);
        if (ret != CVI_SUCCESS) {
            log::error("ZONHOR_MMF_VencSendNv21UserData failed: 0x%x\r\n", ret);
            return new video::Frame();
        }

        ZONHOR_MMF_VENC_STREAM_S mmf_stream;
        memset(&mmf_stream, 0, sizeof(mmf_stream));
        ret = ZONHOR_MMF_VencGetStream(priv->chn, &mmf_stream, ZONHOR_VENC_GET_TIMEOUT_MS);
        if (ret != CVI_SUCCESS) {
            log::error("ZONHOR_MMF_VencGetStream failed: 0x%x\r\n", ret);
            return new video::Frame();
        }

        int stream_size = 0;
        uint8_t *stream_buffer = _merge_venc_stream(&mmf_stream, &stream_size);
        ZONHOR_MMF_VencReleaseStream(priv->chn, &mmf_stream);

        return new video::Frame(stream_buffer, stream_size, pts, dts, 0, true, false);
    }

    err::Err Encoder::push(pipeline::Frame *frame)
    {
        (void)frame;
        zonhor_video_not_impl("Encoder push not migrated on Zonhor yet");
        return err::ERR_NOT_IMPL;
    }

    pipeline::Stream *Encoder::pop(int block_ms)
    {
        (void)block_ms;
        zonhor_enc_priv_t *priv = _enc_priv(_param);
        if (!priv || !priv->bound)
            return nullptr;

        _drain_bound_camera(_camera, NULL);
        video::Frame *frame = _pull_venc_frame(priv, _time_base, &_encode_started,
                                               &_start_encode_ms);
        if (!frame || !frame->is_valid()) {
            delete frame;
            return nullptr;
        }

        /* pipeline::Stream expects mmf_stream_t-like payload — not migrated. */
        delete frame;
        zonhor_video_not_impl("Encoder pop pipeline wrapper not migrated on Zonhor yet");
        return nullptr;
    }

    void *Encoder::get_driver()
    {
        return NULL;
    }

    /* ── Decoder stubs ─────────────────────────────────────────────────── */

    Decoder::Decoder(std::string path, image::Format format)
    {
        (void)path;
        (void)format;
        zonhor_video_not_impl("Decoder not migrated on Zonhor yet");
    }

    Decoder::~Decoder() {}

    video::Context *Decoder::decode_video(bool block)
    {
        (void)block;
        zonhor_video_not_impl("Decoder not migrated on Zonhor yet");
        return nullptr;
    }

    video::Context *Decoder::decode_audio()
    {
        zonhor_video_not_impl("Decoder not migrated on Zonhor yet");
        return nullptr;
    }

    video::Context *Decoder::decode(bool block)
    {
        (void)block;
        zonhor_video_not_impl("Decoder not migrated on Zonhor yet");
        return nullptr;
    }

    err::Err Decoder::push(pipeline::Stream *stream)
    {
        (void)stream;
        return err::ERR_NOT_IMPL;
    }

    pipeline::Frame *Decoder::pop(int block_ms)
    {
        (void)block_ms;
        return nullptr;
    }

    video::Context *Decoder::unpack()
    {
        zonhor_video_not_impl("Decoder not migrated on Zonhor yet");
        return nullptr;
    }

    void *Decoder::get_driver()
    {
        return NULL;
    }

    /* ── Video stubs ───────────────────────────────────────────────────── */

    Video::Video(std::string path, int width, int height, image::Format format,
                 int time_base, int framerate, bool capture, bool open)
    {
        (void)path;
        (void)width;
        (void)height;
        (void)format;
        (void)time_base;
        (void)framerate;
        (void)capture;
        (void)open;
        zonhor_video_not_impl("Video not migrated on Zonhor yet");
    }

    Video::~Video() {}

    err::Err Video::open(std::string path, double fps)
    {
        (void)path;
        (void)fps;
        return err::ERR_NOT_IMPL;
    }

    void Video::close() {}

    err::Err Video::bind_camera(camera::Camera *camera)
    {
        (void)camera;
        return err::ERR_NOT_IMPL;
    }

    video::Packet *Video::encode(image::Image *img)
    {
        (void)img;
        zonhor_video_not_impl("Video not migrated on Zonhor yet");
        return nullptr;
    }

    image::Image *Video::decode(video::Frame *frame)
    {
        (void)frame;
        zonhor_video_not_impl("Video not migrated on Zonhor yet");
        return nullptr;
    }

    err::Err Video::finish()
    {
        return err::ERR_NOT_IMPL;
    }

    /* ── VideoRecorder stubs ───────────────────────────────────────────── */

    VideoRecorder::VideoRecorder(bool open)
    {
        (void)open;
        zonhor_video_not_impl("VideoRecorder not migrated on Zonhor yet");
    }

    VideoRecorder::~VideoRecorder() {}

    err::Err VideoRecorder::lock(int64_t timeout)
    {
        (void)timeout;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::unlock()
    {
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::open()
    {
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::close()
    {
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::bind_display(display::Display *display, image::Fit fit)
    {
        (void)display;
        (void)fit;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::bind_camera(camera::Camera *camera)
    {
        (void)camera;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::bind_audio(audio::Recorder *audio)
    {
        (void)audio;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::bind_imu(void *imu)
    {
        (void)imu;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::reset()
    {
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::config_path(std::string path)
    {
        (void)path;
        return err::ERR_NOT_IMPL;
    }

    std::string VideoRecorder::get_path()
    {
        return std::string();
    }

    err::Err VideoRecorder::config_snapshot(bool enable, std::vector<int> resolution,
                                            image::Format format)
    {
        (void)enable;
        (void)resolution;
        (void)format;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::config_resolution(std::vector<int> resolution)
    {
        (void)resolution;
        return err::ERR_NOT_IMPL;
    }

    std::vector<int> VideoRecorder::get_resolution()
    {
        return std::vector<int>();
    }

    err::Err VideoRecorder::config_fps(int fps)
    {
        (void)fps;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::config_bitrate(int bitrate)
    {
        (void)bitrate;
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::record_start()
    {
        return err::ERR_NOT_IMPL;
    }

    image::Image *VideoRecorder::snapshot()
    {
        return nullptr;
    }

    err::Err VideoRecorder::record_finish()
    {
        return err::ERR_NOT_IMPL;
    }

    err::Err VideoRecorder::draw_rect(int id, int x, int y, int w, int h,
                                        image::Color color, int thickness, bool hidden)
    {
        (void)id;
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)color;
        (void)thickness;
        (void)hidden;
        return err::ERR_NOT_IMPL;
    }

} /* namespace maix::video */
