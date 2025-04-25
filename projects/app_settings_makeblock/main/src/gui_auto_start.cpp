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

static lv_obj_t *label = NULL;

static std::string strip(const std::string& str) {
    auto left = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) { return std::isspace(ch); });
    auto right = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
    if (right <= left) {
        return ""; // All spaces or empty string
    }
    return std::string(left, right);
}

static std::string get_curr_auto_start_app()
{
    std::string res = "";
    const std::string path = "/maixapp/auto_start.txt";
    if (!fs::exists(path))
        return "";
    fs::File *f = fs::open(path, "r");
    if (!f)
        return "";
    std::string *app_name = f->readline();
    if(app_name)
    {
        res = *app_name;
        delete app_name;
    }
    f->close();
    delete f;
    return strip(res);
}

static void set_curr_auto_start_app(const std::string &id)
{
    const std::string path = "/maixapp/auto_start.txt";
    if(id.empty())
    {
        if(fs::exists(path))
            fs::remove(path);
        return;
    }
    fs::File *f = fs::open(path, "w");
    if (!f)
    {
        log::error("open write %s failed", path.c_str());
        return;
    }
    f->write(id.data(), id.size());
    f->close();
    delete f;
    sync();
}

static void set_auto_start_btn_event_cb(lv_event_t *e)
{
    uint32_t code = lv_event_get_code(e);
    app::APP_Info *app_info = (app::APP_Info *)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED)
    {
        set_curr_auto_start_app(app_info->id);
        lv_label_set_text(label, (std::string(_("Current")) + ": " + get_curr_auto_start_app()).c_str());
    }
}

void on_auto_start_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
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
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label = lv_label_create(container);
    lv_label_set_text(label, (std::string(_("Current")) + ": " + get_curr_auto_start_app()).c_str());

    lv_obj_t *cancel_btn = lv_btn_create(container);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, _("Cancel"));
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t *e){
        set_curr_auto_start_app("");
        lv_label_set_text(label, (std::string(_("Current")) + ": ").c_str());
    }, LV_EVENT_CLICKED, NULL);

    vector<app::APP_Info> &apps = app::get_apps_info(true, true);
    int count = 0;
    log::info("apps count: %d\n", apps.size());
    for (auto &i : apps)
    {
        lv_obj_t *btn = lv_btn_create(container);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(btn, theme_btn_color, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_size(btn, lv_pct(70), lv_pct(30));
        lv_obj_set_style_radius(btn, lv_pct(100), LV_PART_MAIN);
        lv_obj_add_event_cb(btn, set_auto_start_btn_event_cb, LV_EVENT_CLICKED, &i);
        // icon
        string path;
        if (!fs::isabs(i.icon))
            path = app::get_app_path(i.id) + "/" + i.icon;
        else
            path = i.icon;
        const char *path_c = path.c_str();
        // judge file is end up with .json or .png, json is lottie animation, png is image
        if (i.icon.substr(i.icon.length() - 4) == "json")
        {
            printf("-- Lottie icon path: %s\n", path_c);
            /*lv_obj_t *icon = */ lv_rlottie_create_from_file(btn, 64, 64, path_c);
        }
        else if (i.icon.substr(i.icon.length() - 4) == ".png")
        {
            lv_obj_t *icon = lv_img_create(btn);
            char buff[1024];
            snprintf(buff, sizeof(buff), "%c:%s", LV_FS_STDIO_LETTER, path_c);
            printf("-- PNG icon path: %s\n", buff);
            lv_img_set_src(icon, buff);
        }
        else
        {
            printf("icon file format not support, only surpport .json and .png\n");
        }
        // name
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, i.name.c_str());
        lv_obj_set_style_text_color(label, theme_text_color, LV_PART_MAIN);
        count += 1;
    }
}

void on_auto_start_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
}
