#pragma once
#include "z_display.hpp"
#include "z_touchscreen.hpp"

namespace z
{
    extern display::Display *z_display;
    extern image::Image     *z_image;
    extern touchscreen::TouchScreen *z_touchscreen;

    /**
     * @brief init lvgl
     * @param display display device, display must init first
    */
    void lvgl_init(display::Display *display, touchscreen::TouchScreen *touchscreen);
//    void lvgl_init(display::Display *display);

    void lvgl_destroy();
}

