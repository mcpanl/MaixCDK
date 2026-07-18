#define _DEFAULT_SOURCE

#include "rk_lib.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>

#define RK_VI_MAX_BUF 8

struct rk_vi_buf {
    unsigned int n_planes;
    void *plane_start[VIDEO_MAX_PLANES];
    size_t plane_length[VIDEO_MAX_PLANES];
};

struct rk_vi_ctx {
    int fd;
    unsigned int width;
    unsigned int height;
    uint32_t pixelformat;
    enum v4l2_buf_type cap_type;
    unsigned int num_buf_planes;
    uint32_t plane_bytesperline[VIDEO_MAX_PLANES];
    unsigned int n_bufs;
    struct rk_vi_buf buf[RK_VI_MAX_BUF];
    int streaming;
};

static int clamp_u8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

static unsigned int rk_pixfmt_plane_count(uint32_t pf)
{
    if (pf == V4L2_PIX_FMT_NV12 || pf == V4L2_PIX_FMT_NV21)
        return 2;
    return 1;
}

static void vi_free_bufs(rk_vi_ctx_t *ctx)
{
    for (unsigned i = 0; i < ctx->n_bufs; i++) {
        for (unsigned p = 0; p < ctx->buf[i].n_planes; p++) {
            if (ctx->buf[i].plane_start[p] && ctx->buf[i].plane_start[p] != MAP_FAILED) {
                munmap(ctx->buf[i].plane_start[p], ctx->buf[i].plane_length[p]);
                ctx->buf[i].plane_start[p] = NULL;
                ctx->buf[i].plane_length[p] = 0;
            }
        }
        ctx->buf[i].n_planes = 0;
    }
    ctx->n_bufs = 0;
}

static int vi_stream_off(rk_vi_ctx_t *ctx)
{
    if (!ctx->streaming)
        return 0;
    enum v4l2_buf_type type = ctx->cap_type;
    if (ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) < 0)
        return -errno;
    ctx->streaming = 0;
    return 0;
}

static int vi_apply_gfmt_planes(rk_vi_ctx_t *ctx, const struct v4l2_format *fmt)
{
    if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        ctx->width = fmt->fmt.pix_mp.width;
        ctx->height = fmt->fmt.pix_mp.height;
        ctx->pixelformat = fmt->fmt.pix_mp.pixelformat;
        ctx->num_buf_planes = fmt->fmt.pix_mp.num_planes;
        if (ctx->num_buf_planes > VIDEO_MAX_PLANES)
            ctx->num_buf_planes = VIDEO_MAX_PLANES;
        for (unsigned p = 0; p < ctx->num_buf_planes; p++)
            ctx->plane_bytesperline[p] = fmt->fmt.pix_mp.plane_fmt[p].bytesperline;
    } else {
        ctx->width = fmt->fmt.pix.width;
        ctx->height = fmt->fmt.pix.height;
        ctx->pixelformat = fmt->fmt.pix.pixelformat;
        ctx->num_buf_planes = 1;
        ctx->plane_bytesperline[0] = fmt->fmt.pix.bytesperline;
    }
    return 0;
}

static int vi_reqbufs_mmap(rk_vi_ctx_t *ctx)
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = ctx->cap_type;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(ctx->fd, VIDIOC_REQBUFS, &req);

    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = ctx->cap_type;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0)
        return -errno;
    if (req.count < 2)
        return -ENOMEM;

    ctx->n_bufs = req.count;
    if (ctx->n_bufs > RK_VI_MAX_BUF)
        ctx->n_bufs = RK_VI_MAX_BUF;

    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    for (unsigned i = 0; i < ctx->n_bufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = ctx->cap_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            memset(planes, 0, sizeof(planes));
            buf.length = ctx->num_buf_planes;
            buf.m.planes = planes;
        }
        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0)
            return -errno;

        if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
            ctx->buf[i].n_planes = 1;
            ctx->buf[i].plane_length[0] = buf.length;
            ctx->buf[i].plane_start[0] =
                mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buf.m.offset);
            if (ctx->buf[i].plane_start[0] == MAP_FAILED) {
                vi_free_bufs(ctx);
                return -errno;
            }
        } else {
            ctx->buf[i].n_planes = ctx->num_buf_planes;
            for (unsigned p = 0; p < ctx->num_buf_planes; p++) {
                ctx->buf[i].plane_length[p] = planes[p].length;
                ctx->buf[i].plane_start[p] = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE,
                                                    MAP_SHARED, ctx->fd, planes[p].m.mem_offset);
                if (ctx->buf[i].plane_start[p] == MAP_FAILED) {
                    vi_free_bufs(ctx);
                    return -errno;
                }
            }
        }
    }
    return 0;
}

int rk_vi_open(rk_vi_ctx_t **out_ctx, const char *device)
{
    const char *path = device ? device : RK_VI_DEFAULT_DEVICE;
    rk_vi_ctx_t *ctx = (rk_vi_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return -ENOMEM;

    ctx->fd = open(path, O_RDWR | O_NONBLOCK, 0);
    if (ctx->fd < 0) {
        int e = errno;
        free(ctx);
        return -e;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        int e = errno;
        close(ctx->fd);
        free(ctx);
        return -e;
    }
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        ctx->cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        ctx->cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else {
        close(ctx->fd);
        free(ctx);
        return -ENOTSUP;
    }

    *out_ctx = ctx;
    return 0;
}

void rk_vi_close(rk_vi_ctx_t *ctx)
{
    if (!ctx)
        return;
    vi_stream_off(ctx);
    vi_free_bufs(ctx);
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    free(ctx);
}

int rk_vi_enum_format(rk_vi_ctx_t *ctx, unsigned int index, rk_vi_format_desc_t *desc)
{
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.index = index;
    fmtdesc.type = ctx->cap_type;
    if (ioctl(ctx->fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
        if (errno == EINVAL)
            return -1;
        return -errno;
    }
    memset(desc, 0, sizeof(*desc));
    strncpy(desc->description, (const char *)fmtdesc.description, sizeof(desc->description) - 1);
    desc->pixelformat = fmtdesc.pixelformat;
    desc->flags = fmtdesc.flags;
    return 0;
}

int rk_vi_enum_size(rk_vi_ctx_t *ctx, uint32_t pixelformat, unsigned int index,
                    rk_vi_size_desc_t *size)
{
    struct v4l2_frmsizeenum fse;
    memset(&fse, 0, sizeof(fse));
    fse.index = index;
    fse.pixel_format = pixelformat;
    if (ioctl(ctx->fd, VIDIOC_ENUM_FRAMESIZES, &fse) < 0) {
        if (errno == EINVAL)
            return -1;
        return -errno;
    }
    if (fse.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        size->width = fse.discrete.width;
        size->height = fse.discrete.height;
        return 0;
    }
    if (fse.type == V4L2_FRMSIZE_TYPE_STEPWISE || fse.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
        if (index == 0) {
            size->width = fse.stepwise.min_width;
            size->height = fse.stepwise.min_height;
            return 0;
        }
        return -1;
    }
    return -ENOTSUP;
}

static int vi_set_format_inner(rk_vi_ctx_t *ctx, uint32_t width, uint32_t height, uint32_t pixelformat)
{
    int r = vi_stream_off(ctx);
    if (r < 0)
        return r;
    vi_free_bufs(ctx);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.type = ctx->cap_type;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = 0;
    ioctl(ctx->fd, VIDIOC_REQBUFS, &req);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = ctx->cap_type;

    if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        unsigned np = rk_pixfmt_plane_count(pixelformat);
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = pixelformat;
        fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
        fmt.fmt.pix_mp.num_planes = (__u8)np;
        if (np == 1) {
            fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
            fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 0;
        } else {
            fmt.fmt.pix_mp.plane_fmt[0].bytesperline = width;
            fmt.fmt.pix_mp.plane_fmt[0].sizeimage = width * height;
            fmt.fmt.pix_mp.plane_fmt[1].bytesperline = width;
            fmt.fmt.pix_mp.plane_fmt[1].sizeimage = width * height / 2;
        }
        if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0)
            return -errno;
    } else {
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = pixelformat;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0)
            return -errno;
    }

    vi_apply_gfmt_planes(ctx, &fmt);
    return vi_reqbufs_mmap(ctx);
}

int rk_vi_set_format(rk_vi_ctx_t *ctx, uint32_t width, uint32_t height, uint32_t pixelformat)
{
    return vi_set_format_inner(ctx, width, height, pixelformat);
}

int rk_vi_get_format(rk_vi_ctx_t *ctx, uint32_t *width, uint32_t *height, uint32_t *pixelformat)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = ctx->cap_type;
    if (ioctl(ctx->fd, VIDIOC_G_FMT, &fmt) < 0)
        return -errno;
    vi_apply_gfmt_planes(ctx, &fmt);
    if (width)
        *width = ctx->width;
    if (height)
        *height = ctx->height;
    if (pixelformat)
        *pixelformat = ctx->pixelformat;
    return 0;
}

int rk_vi_stream_on(rk_vi_ctx_t *ctx)
{
    if (ctx->streaming)
        return 0;
    if (ctx->n_bufs == 0)
        return -EINVAL;

    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    for (unsigned i = 0; i < ctx->n_bufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = ctx->cap_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            memset(planes, 0, sizeof(planes));
            buf.length = ctx->num_buf_planes;
            buf.m.planes = planes;
            for (unsigned p = 0; p < ctx->num_buf_planes; p++)
                planes[p].bytesused = 0;
        }
        if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0)
            return -errno;
    }
    enum v4l2_buf_type type = ctx->cap_type;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0)
        return -errno;
    ctx->streaming = 1;
    return 0;
}

int rk_vi_stream_off(rk_vi_ctx_t *ctx)
{
    return vi_stream_off(ctx);
}

int rk_vi_dequeue(rk_vi_ctx_t *ctx, void **plane, size_t *len, unsigned int *index, int timeout_ms)
{
    if (!ctx->streaming)
        return -EINVAL;

    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    for (;;) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = ctx->cap_type;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            memset(planes, 0, sizeof(planes));
            buf.length = ctx->num_buf_planes;
            buf.m.planes = planes;
        }
        if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) == 0) {
            if (buf.index >= ctx->n_bufs)
                return -EIO;
            if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
                *plane = ctx->buf[buf.index].plane_start[0];
                *len = buf.bytesused;
            } else {
                *plane = ctx->buf[buf.index].plane_start[0];
                *len = planes[0].bytesused;
            }
            *index = buf.index;
            return 0;
        }
        if (errno != EAGAIN)
            return -errno;

        struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN};
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr == 0)
            return -ETIMEDOUT;
        if (pr < 0)
            return -errno;
    }
}

int rk_vi_queue(rk_vi_ctx_t *ctx, unsigned int index)
{
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = ctx->cap_type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    if (ctx->cap_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        memset(planes, 0, sizeof(planes));
        buf.length = ctx->num_buf_planes;
        buf.m.planes = planes;
        for (unsigned p = 0; p < ctx->num_buf_planes; p++)
            planes[p].bytesused = 0;
    }
    if (ioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0)
        return -errno;
    return 0;
}

void *rk_vi_buffer_uv_plane(rk_vi_ctx_t *ctx, unsigned int buf_index)
{
    if (!ctx || buf_index >= ctx->n_bufs || ctx->num_buf_planes < 2)
        return NULL;
    return ctx->buf[buf_index].plane_start[1];
}

uint32_t rk_vi_y_stride_bytes(rk_vi_ctx_t *ctx)
{
    return ctx ? ctx->plane_bytesperline[0] : 0;
}

uint32_t rk_vi_uv_stride_bytes(rk_vi_ctx_t *ctx)
{
    return (ctx && ctx->num_buf_planes > 1) ? ctx->plane_bytesperline[1] : 0;
}

void rk_vi_yuyv_to_rgb888(const uint8_t *yuyv, uint32_t width, uint32_t height, uint32_t yuyv_stride,
                          uint8_t *dst, uint32_t dst_stride)
{
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src = yuyv + y * yuyv_stride;
        uint8_t *d = dst + y * dst_stride;
        for (uint32_t x = 0; x + 1 < width; x += 2) {
            int y0 = (int)src[x * 2 + 0];
            int u = (int)src[x * 2 + 1] - 128;
            int y1 = (int)src[x * 2 + 2];
            int v = (int)src[x * 2 + 3] - 128;

            int r = y0 + ((359 * v) >> 8);
            int g = y0 - ((88 * u + 183 * v) >> 8);
            int b = y0 + ((454 * u) >> 8);
            d[x * 3 + 0] = (uint8_t)clamp_u8(r);
            d[x * 3 + 1] = (uint8_t)clamp_u8(g);
            d[x * 3 + 2] = (uint8_t)clamp_u8(b);

            r = y1 + ((359 * v) >> 8);
            g = y1 - ((88 * u + 183 * v) >> 8);
            b = y1 + ((454 * u) >> 8);
            d[(x + 1) * 3 + 0] = (uint8_t)clamp_u8(r);
            d[(x + 1) * 3 + 1] = (uint8_t)clamp_u8(g);
            d[(x + 1) * 3 + 2] = (uint8_t)clamp_u8(b);
        }
    }
}

void rk_vi_uyvy_to_rgb888(const uint8_t *uyvy, uint32_t width, uint32_t height, uint32_t uyvy_stride,
                          uint8_t *dst, uint32_t dst_stride)
{
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src = uyvy + y * uyvy_stride;
        uint8_t *d = dst + y * dst_stride;
        for (uint32_t x = 0; x + 1 < width; x += 2) {
            int u = (int)src[x * 2 + 0] - 128;
            int y0 = (int)src[x * 2 + 1];
            int v = (int)src[x * 2 + 2] - 128;
            int y1 = (int)src[x * 2 + 3];

            int r = y0 + ((359 * v) >> 8);
            int g = y0 - ((88 * u + 183 * v) >> 8);
            int b = y0 + ((454 * u) >> 8);
            d[x * 3 + 0] = (uint8_t)clamp_u8(r);
            d[x * 3 + 1] = (uint8_t)clamp_u8(g);
            d[x * 3 + 2] = (uint8_t)clamp_u8(b);

            r = y1 + ((359 * v) >> 8);
            g = y1 - ((88 * u + 183 * v) >> 8);
            b = y1 + ((454 * u) >> 8);
            d[(x + 1) * 3 + 0] = (uint8_t)clamp_u8(r);
            d[(x + 1) * 3 + 1] = (uint8_t)clamp_u8(g);
            d[(x + 1) * 3 + 2] = (uint8_t)clamp_u8(b);
        }
    }
}

static void yuv_to_rgb_pixel(int yv, int u, int v, uint8_t *rgb)
{
    int r = yv + ((359 * v) >> 8);
    int g = yv - ((88 * u + 183 * v) >> 8);
    int b = yv + ((454 * u) >> 8);
    rgb[0] = (uint8_t)clamp_u8(r);
    rgb[1] = (uint8_t)clamp_u8(g);
    rgb[2] = (uint8_t)clamp_u8(b);
}

void rk_vi_nv12_to_rgb888(const uint8_t *y_plane, const uint8_t *uv_plane, uint32_t width,
                          uint32_t height, uint32_t y_stride, uint32_t uv_stride, uint8_t *dst,
                          uint32_t dst_stride)
{
    for (uint32_t row = 0; row < height; row++) {
        const uint8_t *yrow = y_plane + row * y_stride;
        const uint8_t *uvrow = uv_plane + (row / 2) * uv_stride;
        uint8_t *d = dst + row * dst_stride;
        for (uint32_t col = 0; col < width; col++) {
            int yv = (int)yrow[col];
            int u = (int)uvrow[(col / 2) * 2 + 0] - 128;
            int v = (int)uvrow[(col / 2) * 2 + 1] - 128;
            yuv_to_rgb_pixel(yv, u, v, d + col * 3);
        }
    }
}

void rk_vi_nv21_to_rgb888(const uint8_t *y_plane, const uint8_t *vu_plane, uint32_t width,
                          uint32_t height, uint32_t y_stride, uint32_t vu_stride, uint8_t *dst,
                          uint32_t dst_stride)
{
    for (uint32_t row = 0; row < height; row++) {
        const uint8_t *yrow = y_plane + row * y_stride;
        const uint8_t *vurow = vu_plane + (row / 2) * vu_stride;
        uint8_t *d = dst + row * dst_stride;
        for (uint32_t col = 0; col < width; col++) {
            int yv = (int)yrow[col];
            int v = (int)vurow[(col / 2) * 2 + 0] - 128;
            int u = (int)vurow[(col / 2) * 2 + 1] - 128;
            yuv_to_rgb_pixel(yv, u, v, d + col * 3);
        }
    }
}
