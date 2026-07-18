#pragma once

#include "rk_lib.h"

namespace maix {
namespace rk {

inline int vi_open(rk_vi_ctx_t **ctx, const char *device = nullptr) {
    return rk_vi_open(ctx, device);
}
inline void vi_close(rk_vi_ctx_t *ctx) { rk_vi_close(ctx); }

inline int vo_open(rk_vo_ctx_t **ctx, const char *display, unsigned w, unsigned h) {
    return rk_vo_open(ctx, display, w, h);
}
inline int vo_open(rk_vo_ctx_t **ctx, const char *display, unsigned w, unsigned h, const char *title) {
    return rk_vo_open2(ctx, display, w, h, title);
}
inline void vo_close(rk_vo_ctx_t *ctx) { rk_vo_close(ctx); }

} // namespace rk
} // namespace maix
