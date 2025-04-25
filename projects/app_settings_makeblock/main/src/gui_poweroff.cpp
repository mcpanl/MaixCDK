#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_thread.hpp"
#include "maix_app.hpp"
#include "maix_gui.hpp"
#include "maix_basic.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>

using namespace maix;

void on_poweroff_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
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
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ok = lv_btn_create(container);
    lv_obj_t *label_ok = lv_label_create(ok);
    lv_label_set_text(label_ok, _("Power Off"));
    lv_obj_t *reboot = lv_btn_create(container);
    lv_obj_t *label_reboot = lv_label_create(reboot);
    lv_label_set_text(label_reboot, _("Reboot"));

    lv_obj_add_event_cb(ok, [](lv_event_t *e) {
        lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
        lv_msgbox_add_title(mbox, _("Power Off"));
        lv_msgbox_add_text(mbox, _("Poweroff Now"));
        lv_msgbox_add_close_button(mbox);
        lv_obj_center(mbox);
        sys::poweroff();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(reboot, [](lv_event_t *e) {
        lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
        lv_msgbox_add_title(mbox, _("Reboot"));
        lv_msgbox_add_text(mbox, _("Reboot Now"));
        lv_msgbox_add_close_button(mbox);
        sys::reboot();
    }, LV_EVENT_CLICKED, NULL);
}

void on_poweroff_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
}
