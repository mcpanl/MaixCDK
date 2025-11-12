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

static lv_obj_t *ddlist = nullptr;

    const static std::vector<std::string> comm_methods = {
        "uart",
        // "tcp",
        "none"
    };


void on_comm_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    std::string locale = i18n::get_locale();

    lv_obj_t *root = (lv_obj_t *)obj;
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_set_style_bg_color(root, theme_bg_color, LV_PART_MAIN);
    // add a drop down list
    ddlist = lv_dropdown_create(root);
    lv_obj_set_size(ddlist, 200, 50);
    lv_obj_align(ddlist, LV_ALIGN_CENTER, 0, 0);
    std::string langs_str = "";
    for (auto &lang : comm_methods)
    {
        langs_str += lang + "\n";
    }
    // set font
    lv_obj_set_style_text_font(ddlist, &lv_font_montserrat_16, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ddlist, theme_btn_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(ddlist, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_color(ddlist, theme_btn_color, LV_PART_MAIN);

    lv_dropdown_set_options(ddlist, langs_str.c_str());
    lv_obj_add_event_cb(ddlist, [](lv_event_t *e) {
        uint32_t code = lv_event_get_code(e);
        /*if (code == LV_EVENT_VALUE_CHANGED)
        {
            lv_obj_t *ddlist = lv_event_get_current_target(e);
            uint16_t idx = lv_dropdown_get_selected(ddlist);
            printf("select language: %s, %s\n", locales[idx].c_str(), langs_name[idx].c_str());
        }
        else */if (code == LV_EVENT_READY)
        {
            lv_obj_t *ddlist = (lv_obj_t *)lv_event_get_current_target(e);
            lv_obj_t *list = lv_dropdown_get_list(ddlist);
            lv_obj_set_style_bg_color(list, theme_btn_color, LV_PART_MAIN);
            lv_obj_set_style_text_color(list, lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_border_color(list, theme_btn_color, LV_PART_MAIN);
        }
    }, LV_EVENT_READY, NULL);

    // load locale from config file
    string method = app::get_sys_config_kv("comm", "method", "uart");
    for (size_t i = 0; i < comm_methods.size(); i++)
    {
        if (comm_methods[i] == method)
        {
            lv_dropdown_set_selected(ddlist, i);
            break;
        }
    }

    // create a confirm button
    lv_obj_t *btn = lv_btn_create(root);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 100);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, trans.tr("Confirm").c_str());
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        uint32_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED)
        {
            // lv_obj_t *btn = lv_event_get_current_target(e);
            uint16_t idx = lv_dropdown_get_selected(ddlist);
            log::info("set communicate method: %s\n", comm_methods[idx].c_str());
            // save to config file
            app::set_sys_config_kv("comm", "method", comm_methods[idx]);
            // show toast
            static char buff[12];
            static char *btns[2] = {buff, (char*)""};
            // copy trans.tr("OK").c_str() to btns[0]

            const char *c = trans.tr("OK").c_str();
            size_t len = strlen(c);
            strncpy(btns[0], c, len >= sizeof(buff) ? sizeof(buff) - 1 : len);
            btns[0][len] = '\0';
            lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
            lv_msgbox_add_title(mbox, "");
            lv_msgbox_add_text(mbox, (trans.tr("Set complete")).c_str());
            lv_msgbox_add_close_button(mbox);
            lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, btns[0]);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
                lv_msgbox_close(mbox);
            }, LV_EVENT_CLICKED, mbox);
            lv_obj_center(mbox);
        }
    }, LV_EVENT_CLICKED, NULL);

}

void on_comm_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
}
