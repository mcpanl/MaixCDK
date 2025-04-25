#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_thread.hpp"
#include "maix_basic.hpp"
#include "maix_log.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>

static lv_obj_t *ddlist_region = NULL;
static lv_obj_t *ddlist_cities = NULL;
static lv_obj_t *label_datetime = NULL;
static bool need_exit = false;
std::map<std::string, std::vector<std::string>> timezones;
std::vector<std::string> curr_tz;
thread::Thread *th_gettime = NULL;

void gettime_process(void *args)
{
    // label_datetime
    uint64_t last_t = 0;
    while(!need_exit && !app::need_exit())
    {
        if(time::time_s() - last_t >= 1)
        {
            time::DateTime *dt = time::localtime();
            if(dt)
            {
                while (!lv_ui_mutex_lock(100))
                {
                    time::sleep_ms(1);
                    if (need_exit)
                        break;
                }
                if (need_exit)
                    break;
                lv_label_set_text(label_datetime, dt->strftime("%Y-%m-%d %H:%M:%S").c_str());
                lv_ui_mutex_unlock();
            }
            last_t = time::time_s();
        }
        time::sleep_ms(30);
    }
    log::info("get time process exit");
}

void on_datetime_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    need_exit = false;
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
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // show datetime
    label_datetime = lv_label_create(container);
    lv_label_set_text(label_datetime, "");
    th_gettime = new thread::Thread(gettime_process, NULL);

    // timezone settings lable
    lv_obj_t *label_mode = lv_label_create(container);
    lv_label_set_text(label_mode, _("Timezone Setting"));

    // rows
    lv_obj_t *row1 = lv_obj_create(container);
    lv_obj_set_size(row1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *row2 = lv_obj_create(container);
    lv_obj_set_size(row2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // regions
    lv_obj_t *label_region = lv_label_create(row1);
    lv_label_set_text(label_region, _("Region"));
    // add a drop down list
    ddlist_region = lv_dropdown_create(row1);
    lv_obj_set_size(ddlist_region, 200, 50);
    lv_obj_align(ddlist_region, LV_ALIGN_CENTER, 0, 0);

    timezones = time::list_timezones();
    curr_tz = time::timezone2();
    std::string options = "";
    int curr_region_idx = 0;
    size_t count = 0;
    for (const auto &pair : timezones)
    {
        options += pair.first;
        if (count < timezones.size() - 1)
            options += "\n";
        if (curr_tz.size() >= 2 && curr_tz[0] == pair.first)
            curr_region_idx = count;
        ++count;
    }
    lv_dropdown_set_options(ddlist_region, options.c_str());
    lv_dropdown_set_selected(ddlist_region, curr_region_idx);

    // cities
    lv_obj_t *label_cities = lv_label_create(row2);
    lv_label_set_text(label_cities, _("City"));
    ddlist_cities = lv_dropdown_create(row2);
    lv_obj_set_size(ddlist_cities, 200, 50);
    lv_obj_align(ddlist_cities, LV_ALIGN_CENTER, 0, 0);
    options = "";
    int curr_city_idx = 0;
    if (curr_tz.size() >= 2)
    {
        for (const auto &pair : timezones)
        {
            if (pair.first == curr_tz[0])
            {
                size_t count = 0;
                for (const auto &city : pair.second)
                {
                    options += city;
                    if (count < pair.second.size() - 1)
                        options += "\n";
                    if (curr_tz[0] == pair.first && curr_tz[1] == city)
                        curr_city_idx = count;
                    ++count;
                }
                break;
            }
        }
    }
    lv_dropdown_set_options(ddlist_cities, options.c_str());
    lv_dropdown_set_selected(ddlist_cities, curr_city_idx);

    // chnage region event
    lv_obj_add_event_cb(ddlist_region, [](lv_event_t *e)
                        {
        lv_obj_t *ddlist = (lv_obj_t *)lv_event_get_current_target(e);
        uint16_t idx = lv_dropdown_get_selected(ddlist);
        std::string options = "";
        int curr_city_idx = -1;
        int china_idx = -1;
        if (curr_tz.size() >= 2)
        {
            int count = 0;
            for (const auto &pair : timezones)
            {
                if(count++ == idx)
                {
                    size_t count2 = 0;
                    for (const auto &city : pair.second)
                    {
                        options += city;
                        if (count2 < pair.second.size() - 1)
                            options += "\n";
                        if (curr_tz[0] == pair.first && curr_tz[1] == city)
                            curr_city_idx = count2;
                        else if(china_idx < 0 && pair.first == "Asia" && (city == "Chongqing" || city == "Shanghai"))
                            china_idx = count2;
                        ++count2;
                    }
                    break;
                }
            }
            if (curr_city_idx == -1 && china_idx >= 0)
            {
                curr_city_idx = china_idx;
            }
        }
        lv_dropdown_set_options(ddlist_cities, options.c_str());
        lv_dropdown_set_selected(ddlist_cities, curr_city_idx < 0 ? 0 : curr_city_idx);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // create a confirm button
    lv_obj_t *btn = lv_btn_create(container);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 100);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, trans.tr("Confirm").c_str());
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *e)
                        {
        uint32_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED)
        {
            uint16_t idx_region = lv_dropdown_get_selected(ddlist_region);
            uint16_t idx_city = lv_dropdown_get_selected(ddlist_cities);
            int count = 0;
            for(const auto& pair : timezones)
            {
                if(count++ == idx_region)
                {
                    int count2 = 0;
                    for(const auto& city : pair.second)
                    {
                        if(count2++ == idx_city)
                        {
                            log::info("set region to: %s/%s", pair.first.c_str(), city.c_str());
                            time::timezone2(pair.first, city);
                        }
                    }
                }
            }

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
            lv_msgbox_add_text(mbox, (trans.tr("Set complete") + ", " + trans.tr("Restart APP to apply")).c_str());
            lv_msgbox_add_close_button(mbox);
            lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, btns[0]);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
                lv_msgbox_close(mbox);
            }, LV_EVENT_CLICKED, mbox);
            lv_obj_center(mbox);
        } }, LV_EVENT_CLICKED, NULL);
}

void on_datetime_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    if(th_gettime)
    {
        need_exit = true;
        log::info("wait get time process exit");
        th_gettime->join();
        log::info("wait get time process exit done");
        delete th_gettime;
        th_gettime = NULL;
    }
}
