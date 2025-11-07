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

static int hres = 552;
static int vres = 368;

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
    uint8_t * color_p = px_map;

    if(area->x2 < 0 || area->y2 < 0 || area->x1 >= hres || area->y1 >= vres) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    uint32_t w = lv_area_get_width(area);
    uint32_t line_bytes = w * 3;   // ✅ 每行像素 * 3 bytes (RGB888)


    // swap R and B
    for(int32_t y = area->y1; y <= area->y2 && y < vres; y++) {

        uint8_t * dest = (uint8_t*)z::z_image->data() + (y * hres + area->x1) * 3;
        uint8_t * src = color_p;

        uint32_t x = 0;

        // 每次处理 3 像素 (9 bytes)
        while (x + 3 <= w) {
            uint32_t p0 = src[0] | (src[1] << 8) | (src[2] << 16);
            uint32_t p1 = src[3] | (src[4] << 8) | (src[5] << 16);
            uint32_t p2 = src[6] | (src[7] << 8) | (src[8] << 16);

            // 交换 R / B
            p0 = (p0 & 0x00FF00) | ((p0 & 0xFF0000) >> 16) | ((p0 & 0x0000FF) << 16);
            p1 = (p1 & 0x00FF00) | ((p1 & 0xFF0000) >> 16) | ((p1 & 0x0000FF) << 16);
            p2 = (p2 & 0x00FF00) | ((p2 & 0xFF0000) >> 16) | ((p2 & 0x0000FF) << 16);

            dest[0] = p0 & 0xFF;
            dest[1] = (p0 >> 8) & 0xFF;
            dest[2] = (p0 >> 16) & 0xFF;

            dest[3] = p1 & 0xFF;
            dest[4] = (p1 >> 8) & 0xFF;
            dest[5] = (p1 >> 16) & 0xFF;

            dest[6] = p2 & 0xFF;
            dest[7] = (p2 >> 8) & 0xFF;
            dest[8] = (p2 >> 16) & 0xFF;

            src += 9;
            dest += 9;
            x += 3;
        }

        // 处理剩余像素（不足 3）
        for(; x < w; x++) {
            dest[0] = src[2];
            dest[1] = src[1];
            dest[2] = src[0];
            src += 3;
            dest += 3;
        }

        color_p += w * 3;
    }

//    for(int32_t y = area->y1; y <= area->y2 && y < vres; y++) {
//
//        uint8_t * dest = (uint8_t*)z::z_image->data() + (y * hres + area->x1) * 3; // ✅ RGB888 地址计算
//        memcpy(dest, color_p, line_bytes);
//
//        color_p += line_bytes;
//    }

    if(lv_disp_flush_is_last(disp_drv)) {
        z::z_display->show(*z::z_image);  // ✅ 直接输出 RGB888
    }

    lv_disp_flush_ready(disp_drv);
}

// #endif /*USE_MONITOR*/
