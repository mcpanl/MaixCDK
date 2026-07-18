/**
 * @file monitor.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "monitor.h"
// #if USE_MONITOR

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "mouse.h"
#include "keyboard.h"
#include "mousewheel.h"
#include "z_lvgl.hpp"
#include "maix_basic.hpp"

#include <stdexcept>

using namespace maix;

static int hres = 640;
static int vres = 480;
static uint64_t last_present_ms = 0;
static constexpr uint32_t present_interval_ms = 16;  // ~60fps for smoother scrolling

// FPS logging
static uint64_t fps_frame_count = 0;
static uint64_t fps_last_log_ms = 0;
static constexpr uint32_t fps_log_interval_ms = 5000; // Log every 5 seconds

static inline void copy_bgr888_to_rgb888(uint8_t *dest, const uint8_t *src, uint32_t pixel_count)
{
    for(uint32_t x = 0; x < pixel_count; ++x) {
        dest[0] = src[2];
        dest[1] = src[1];
        dest[2] = src[0];
        src += 3;
        dest += 3;
    }
}

/**
 * Initialize the monitor
 */
void monitor_init(int w, int h)
{
    hres = w;
    vres = h;
}

void monitor_rect(int* w, int* h)
{
    if (nullptr != w)
        *w = hres;
    if (nullptr != h)
        *h = vres;
}

void monitor_flush(lv_display_t *disp_drv, const lv_area_t * area, uint8_t *px_map)
{
    assert(LV_COLOR_DEPTH == 24); // RGB888
    if (!z_image || !z_display || !z_image->data()) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    if(area->x2 < 0 || area->y2 < 0 || area->x1 >= hres || area->y1 >= vres) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    const int32_t src_width = lv_area_get_width(area);
    const int32_t clip_x1 = area->x1 < 0 ? 0 : area->x1;
    const int32_t clip_y1 = area->y1 < 0 ? 0 : area->y1;
    const int32_t clip_x2 = area->x2 >= hres ? hres - 1 : area->x2;
    const int32_t clip_y2 = area->y2 >= vres ? vres - 1 : area->y2;

    if (clip_x1 > clip_x2 || clip_y1 > clip_y2) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    const uint32_t copy_width = (uint32_t)(clip_x2 - clip_x1 + 1);
    const uint32_t src_row_bytes = (uint32_t)src_width * 3;
    uint8_t *src_row = px_map + (uint32_t)(clip_y1 - area->y1) * src_row_bytes;

    for(int32_t y = clip_y1; y <= clip_y2; ++y) {
        uint8_t *dest = (uint8_t*)z_image->data() + ((size_t)y * hres + clip_x1) * 3;
        const uint8_t *src = src_row + (uint32_t)(clip_x1 - area->x1) * 3;
        copy_bgr888_to_rgb888(dest, src, copy_width);
        src_row += src_row_bytes;
    }

    if(lv_disp_flush_is_last(disp_drv)) {
        uint64_t now = time::ticks_ms();
        if (last_present_ms != 0) {
            uint64_t elapsed = now - last_present_ms;
            if (elapsed < present_interval_ms) {
                time::sleep_ms((uint32_t)(present_interval_ms - elapsed));
            }
        }

        z_display->show(*z_image, image::FIT_FILL);
        last_present_ms = time::ticks_ms();

        // FPS logging
        fps_frame_count++;
        if (fps_last_log_ms == 0) {
            fps_last_log_ms = now;
        } else if (now - fps_last_log_ms >= fps_log_interval_ms) {
            uint64_t elapsed = now - fps_last_log_ms;
            float fps = (float)fps_frame_count * 1000.0f / (float)elapsed;
            log::info("[LVGL FPS] avg: %.1f (%lu frames in %lu ms)", fps, (unsigned long)fps_frame_count, (unsigned long)elapsed);
            fps_frame_count = 0;
            fps_last_log_ms = now;
        }
    }

    lv_disp_flush_ready(disp_drv);
}

// #endif /*USE_MONITOR*/
