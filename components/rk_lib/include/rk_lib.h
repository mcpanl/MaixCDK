/**
 * @file rk_lib.h
 * @brief RK3566 平台底层视频输入 (V4L2) 与简易显示 (X11) — 仅公开 C API，便于发布 .so。
 */
#ifndef RK_LIB_H
#define RK_LIB_H

#include <stddef.h>
#include <stdint.h>

/** Linux V4L2 fourcc（与 linux/videodev2.h 中 V4L2_PIX_FMT_* 数值一致） */
#define RK_V4L2_FOURCC(a, b, c, d)                                                     \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define RK_PIX_FMT_YUYV RK_V4L2_FOURCC('Y', 'U', 'Y', 'V')
#define RK_PIX_FMT_UYVY RK_V4L2_FOURCC('U', 'Y', 'V', 'Y')
#define RK_PIX_FMT_NV12 RK_V4L2_FOURCC('N', 'V', '1', '2')
#define RK_PIX_FMT_NV21 RK_V4L2_FOURCC('N', 'V', '2', '1')
#define RK_PIX_FMT_MJPEG RK_V4L2_FOURCC('M', 'J', 'P', 'G')
#define RK_PIX_FMT_RGB24 RK_V4L2_FOURCC('R', 'G', 'B', '3')

#ifdef __cplusplus
extern "C" {
#endif

/** 默认设备节点 */
#define RK_VI_DEFAULT_DEVICE "/dev/video0"

/* ------------------------------------------------------------------------- */
/* V4L2 视频输入                                                             */
/* ------------------------------------------------------------------------- */

typedef struct rk_vi_ctx rk_vi_ctx_t;

/** 枚举到的像素格式描述（fourcc 与 V4L2 一致，如 YUYV、MJPEG） */
typedef struct rk_vi_format_desc {
    char description[64];
    uint32_t pixelformat; /* V4L2_PIX_FMT_* */
    uint32_t flags;
} rk_vi_format_desc_t;

/** 某像素格式下支持的帧尺寸 */
typedef struct rk_vi_size_desc {
    uint32_t width;
    uint32_t height;
} rk_vi_size_desc_t;

/**
 * 打开 V4L2 设备（未设置格式、未起流）。
 * @param device 如 "/dev/video0"，NULL 则使用 RK_VI_DEFAULT_DEVICE
 * @return 0 成功，<0 失败（通常为 -errno）
 */
int rk_vi_open(rk_vi_ctx_t **out_ctx, const char *device);

/** 关闭设备并释放 mmap 缓冲 */
void rk_vi_close(rk_vi_ctx_t *ctx);

/**
 * 按索引枚举捕获格式（0,1,2,...），直到返回 -1 表示结束。
 * @return 0 成功写入 desc，-1 枚举结束，<-1 错误
 */
int rk_vi_enum_format(rk_vi_ctx_t *ctx, unsigned int index, rk_vi_format_desc_t *desc);

/**
 * 枚举某 fourcc 下的分辨率（需先知道 pixelformat）。
 * index 从 0 递增直到返回 -1。
 */
int rk_vi_enum_size(rk_vi_ctx_t *ctx, uint32_t pixelformat, unsigned int index,
                    rk_vi_size_desc_t *size);

/**
 * 设置捕获格式并分配 mmap 缓冲（会关闭已存在的流）。
 * @param pixelformat V4L2 fourcc，如 V4L2_PIX_FMT_YUYV
 */
int rk_vi_set_format(rk_vi_ctx_t *ctx, uint32_t width, uint32_t height, uint32_t pixelformat);

/** 查询当前驱动选定的实际格式（S_FMT 后可能与请求略有不同） */
int rk_vi_get_format(rk_vi_ctx_t *ctx, uint32_t *width, uint32_t *height, uint32_t *pixelformat);

/** 起流（QBUF 后 STREAMON） */
int rk_vi_stream_on(rk_vi_ctx_t *ctx);

/** 停流 */
int rk_vi_stream_off(rk_vi_ctx_t *ctx);

/**
 * 取一帧（阻塞直到 timeout_ms 毫秒，-1 表示无限等待）。
 * @param plane 驱动 mmap 起始地址
 * @param len   本帧字节长度
 * @param index buffer 索引，用于 rk_vi_queue
 */
int rk_vi_dequeue(rk_vi_ctx_t *ctx, void **plane, size_t *len, unsigned int *index, int timeout_ms);

/** 归还 buffer */
int rk_vi_queue(rk_vi_ctx_t *ctx, unsigned int index);

/** NV12/NV21 第二平面（UV/VU），与 rk_vi_dequeue 返回的 Y 同属 buf_index */
void *rk_vi_buffer_uv_plane(rk_vi_ctx_t *ctx, unsigned int buf_index);

/** 当前协商格式的每行字节数（G_FMT/S_FMT 后有效） */
uint32_t rk_vi_y_stride_bytes(rk_vi_ctx_t *ctx);
uint32_t rk_vi_uv_stride_bytes(rk_vi_ctx_t *ctx);

/**
 * 将 YUYV 转为 RGB888（逐行），用于无硬件解码时送显。
 * @param dst 长度至少 width * height * 3
 */
void rk_vi_yuyv_to_rgb888(const uint8_t *yuyv, uint32_t width, uint32_t height, uint32_t yuyv_stride,
                          uint8_t *dst, uint32_t dst_stride);

void rk_vi_uyvy_to_rgb888(const uint8_t *uyvy, uint32_t width, uint32_t height, uint32_t uyvy_stride,
                          uint8_t *dst, uint32_t dst_stride);

void rk_vi_nv12_to_rgb888(const uint8_t *y_plane, const uint8_t *uv_plane, uint32_t width,
                          uint32_t height, uint32_t y_stride, uint32_t uv_stride, uint8_t *dst,
                          uint32_t dst_stride);

void rk_vi_nv21_to_rgb888(const uint8_t *y_plane, const uint8_t *vu_plane, uint32_t width,
                          uint32_t height, uint32_t y_stride, uint32_t vu_stride, uint8_t *dst,
                          uint32_t dst_stride);

/* ------------------------------------------------------------------------- */
/* X11 简易显示（需板端有 X 与 DISPLAY）                                      */
/* ------------------------------------------------------------------------- */

typedef struct rk_vo_ctx rk_vo_ctx_t;

/**
 * 创建并映射窗口（同步创建 XImage）。
 * @param display_name 可为 NULL 使用默认 DISPLAY
 * @param w,h 窗口像素大小，例如 640x480
 */
int rk_vo_open(rk_vo_ctx_t **out_ctx, const char *display_name, unsigned int w, unsigned int h);

/**
 * 创建并映射窗口（支持设置标题）。
 * @param title 窗口标题，NULL/空字符串时使用默认标题（可执行文件名）
 */
int rk_vo_open2(rk_vo_ctx_t **out_ctx, const char *display_name, unsigned int w, unsigned int h,
                const char *title);

void rk_vo_close(rk_vo_ctx_t *ctx);

/**
 * 显示一帧 RGB888（自上而下，stride>=width*3）。
 * 若与窗口尺寸不一致，按比例缩放并居中显示，多余区域填黑色（letterbox/pillarbox）。
 */
int rk_vo_put_rgb888(rk_vo_ctx_t *ctx, const uint8_t *rgb, unsigned int w, unsigned int h,
                     unsigned int stride);

/**
 * 处理 X 事件（Expose 重绘、关闭窗口等）。
 * @return 1 继续，0 用户请求关闭窗口，<0 错误
 */
int rk_vo_poll_events(rk_vo_ctx_t *ctx, int block);

/**
 * 在窗口客户区叠加绘制 Latin-1 文本（依赖 X 字体 fixed/6x13 等）。
 * 建议在每次 rk_vo_put_rgb888 之后再调用，以便文字在图像之上。
 * 支持用 '\\n' 换行（最多约 6 行）。
 * @param x,y 首行左上角像素（y 为顶边，内部会换算为字体的 baseline）
 * @param white_fg 非 0 为白字，0 为黑字
 * @return 0 成功，<0 失败（如无可用字体）
 */
int rk_vo_draw_string(rk_vo_ctx_t *ctx, int x, int y, const char *s, int white_fg);

/**
 * Query whether window-local mouse/touch events are available.
 * @param ctx may be NULL to use current active rk_vo context.
 * @param timeout_ms -1 block forever, 0 non-blocking, >0 wait milliseconds.
 * @return 1 when available, 0 when timeout/no event, <0 on error.
 */
int rk_vo_touch_available(rk_vo_ctx_t *ctx, int timeout_ms);

/**
 * Read latest/next window-local mouse/touch event.
 * @param ctx may be NULL to use current active rk_vo context.
 * @param x,y output coordinates in window client area.
 * @param pressed output pressed state (0/1).
 * @param drain_nonkey 1: drain queue and return latest event (read semantics);
 *                     0: return first queued event (read0 semantics).
 * @return 0 success, -EAGAIN when no event, <0 on error.
 */
int rk_vo_touch_read(rk_vo_ctx_t *ctx, int *x, int *y, int *pressed, int drain_nonkey);

#ifdef __cplusplus
}
#endif

#endif /* RK_LIB_H */
