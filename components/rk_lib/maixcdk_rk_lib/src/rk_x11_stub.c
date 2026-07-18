/**
 * Stubs when rk_lib is built with RK_LIB_WITH_X11=OFF (no libX11 in toolchain/sysroot).
 */
#include "rk_lib.h"

#include <errno.h>

int rk_vo_open(rk_vo_ctx_t **out_ctx, const char *display_name, unsigned int w, unsigned int h)
{
    (void)display_name;
    (void)w;
    (void)h;
    if (out_ctx)
        *out_ctx = NULL;
    return -ENOSYS;
}

int rk_vo_open2(rk_vo_ctx_t **out_ctx, const char *display_name, unsigned int w, unsigned int h,
                const char *title)
{
    (void)title;
    return rk_vo_open(out_ctx, display_name, w, h);
}

void rk_vo_close(rk_vo_ctx_t *ctx)
{
    (void)ctx;
}

int rk_vo_put_rgb888(rk_vo_ctx_t *ctx, const uint8_t *rgb, unsigned int w, unsigned int h,
                     unsigned int stride)
{
    (void)ctx;
    (void)rgb;
    (void)w;
    (void)h;
    (void)stride;
    return -ENOSYS;
}

int rk_vo_poll_events(rk_vo_ctx_t *ctx, int block)
{
    (void)ctx;
    (void)block;
    return -EINVAL;
}

int rk_vo_draw_string(rk_vo_ctx_t *ctx, int x, int y, const char *s, int white_fg)
{
    (void)ctx;
    (void)x;
    (void)y;
    (void)s;
    (void)white_fg;
    return -ENOSYS;
}

int rk_vo_touch_available(rk_vo_ctx_t *ctx, int timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    return -ENOSYS;
}

int rk_vo_touch_read(rk_vo_ctx_t *ctx, int *x, int *y, int *pressed, int drain_nonkey)
{
    (void)ctx;
    (void)x;
    (void)y;
    (void)pressed;
    (void)drain_nonkey;
    return -ENOSYS;
}
