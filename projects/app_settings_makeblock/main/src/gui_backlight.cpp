#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_thread.hpp"
#include "maix_app.hpp"
#include "maix_gui.hpp"
#include "maix_time.hpp"
#include "maix_log.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>

static void slider_event_cb(lv_event_t *e);
static lv_obj_t *slider_label = NULL;

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    char buf[8];
    int value = (int)lv_slider_get_value(slider);
    if(value < 5)
        value = 5;
    lv_snprintf(buf, sizeof(buf), "%d%%", value);
    lv_label_set_text(slider_label, buf);
    p_display->set_backlight(value);
    app::set_sys_config_kv("backlight", "value", std::to_string(value));
}

void on_backlight_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    std::string locale = i18n::get_locale();

    lv_obj_t *root = (lv_obj_t *)obj;
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(container, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(container, theme_text_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label = lv_label_create(container);
    lv_label_set_text(label, _("Backlight"));

    lv_obj_t *slider = lv_slider_create(container);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    std::string bl_v_str = app::get_sys_config_kv("backlight", "value");
    float bl_v = 50;
    try
    {
        if(!bl_v_str.empty())
            bl_v = atof(bl_v_str.c_str());
    }
    catch(...)
    {
        bl_v = 50;
    }
    lv_slider_set_value(slider, int(bl_v), LV_ANIM_OFF);

    lv_obj_set_style_anim_duration(slider, 2000, 0);
    /*Create a label below the slider*/
    slider_label = lv_label_create(container);
    lv_label_set_text(slider_label, (std::to_string((int)bl_v) + "%").c_str());

}

void on_backlight_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
}
