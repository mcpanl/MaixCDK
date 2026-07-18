#include "gui.hpp"
#include "lvgl.h"
#include "rlottie/lv_rlottie.h"
#include "run_app.hpp"
#include "maix_app.hpp"
#include "maix_basic.hpp"
#include "i18n.hpp"
#include "maix_pmu.hpp"
#include "maix_key.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/types.h>

using namespace maix;

/* 必须在全局作用域声明；若在 launcher::create_home_items 内声明，C++ 会解析为 launcher::img_xtool，与 C 编译的 img_xtool.c 符号不一致 */
extern "C" {
LV_IMG_DECLARE(img_xtool);
}

/** 1 = 顶栏始终占位显示；0 = 仅在有系统提示（如扩容）时显示顶栏 */
#ifndef MAIX_LAUNCHER_ALWAYS_SHOW_TOP_BAR
#define MAIX_LAUNCHER_ALWAYS_SHOW_TOP_BAR 1
#endif

static thread::Thread *bar_update_thread = nullptr;
static thread::Thread *resize_monitor_thread = nullptr;
static ext_dev::pmu::PMU *pmu = nullptr;
static peripheral::key::Key *powerkey = nullptr;
static bool gui_destroyed = true;
static bool bar_update_thread_exit = true;
static bool resize_monitor_thread_exit = true;

static lv_obj_t *g_top_bar = nullptr;
static lv_obj_t *g_bar_main_label = nullptr;
static lv_obj_t *g_bar_main_img = nullptr;
static lv_obj_t *g_bar_bat_pct = nullptr;
static lv_obj_t *g_bar_bat_icon = nullptr;
static lv_obj_t *g_desktop_cont = nullptr;
static int g_bar_height_pct = 10;

static std::mutex g_system_banner_mutex;
static std::string g_system_banner;

static bool proc_comm_equals(pid_t pid, const char *name)
{
    char path[64];
    char buf[32];
    snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = '\0';
    return std::strcmp(buf, name) == 0;
}

static bool is_resize2fs_running()
{
    DIR *d = opendir("/proc");
    if (!d)
        return false;
    struct dirent *de;
    while ((de = readdir(d)) != nullptr) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        pid_t pid = (pid_t)atoi(de->d_name);
        if (pid <= 1)
            continue;
        if (proc_comm_equals(pid, "resize2fs")) {
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}

static void apply_top_bar_layout(bool bar_visible)
{
    if (!g_top_bar || !g_desktop_cont)
        return;
    if (bar_visible) {
        lv_obj_clear_flag(g_top_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_desktop_cont, 0, lv_pct(g_bar_height_pct));
        lv_obj_set_size(g_desktop_cont, lv_pct(100), lv_pct(100 - g_bar_height_pct));
    } else {
        lv_obj_add_flag(g_top_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_desktop_cont, 0, 0);
        lv_obj_set_size(g_desktop_cont, lv_pct(100), lv_pct(100));
    }
}

static void resize2fs_monitor_process(void *)
{
    resize_monitor_thread_exit = false;
    uint64_t resize_start_ms = 0;
    

    while (!gui_destroyed) {
        bool found = is_resize2fs_running();
        {
            std::lock_guard<std::mutex> lock(g_system_banner_mutex);
            if (found) {
                if (resize_start_ms == 0)
                    resize_start_ms = time::ticks_ms();
                uint32_t elapsed_s = (uint32_t)((time::ticks_ms() - resize_start_ms) / 1000);
                unsigned mm = elapsed_s / 60;
                unsigned ss = elapsed_s % 60;
                char buf[160];
                snprintf(buf, sizeof(buf),
                    "Resizing partition, UI may stutter (%02u:%02u)", mm, ss);
                g_system_banner.assign(buf);
            } else {
                resize_start_ms = 0;
                g_system_banner.clear();
            }
        }
        time::sleep_ms(500);
    }

    {
        std::lock_guard<std::mutex> lock(g_system_banner_mutex);
        g_system_banner.clear();
    }
    resize_monitor_thread_exit = true;
}

#define theme_bg_color lv_color_hex(0x000000)

typedef enum
{
    ICON_TYPE_NONE = 0,
    ICON_TYPE_PNG,
    ICON_TYPE_LOTTIE,
    ICON_TYPE_GIF,
} icon_type_t;

typedef struct
{
    lv_obj_t *obj;
    icon_type_t type;
    int row;
} icon_info_t;

typedef struct
{
    int num;
    int row_num;
    icon_info_t *objs;
} icons_info_t;

extern "C"
{
    static bool g_is_scrolling = false;

    static void update_visible_lottie_playback(icons_info_t *icons_info, lv_obj_t *cont, bool force_pause)
    {
        lv_area_t cont_a;
        lv_obj_get_coords(cont, &cont_a);
        static int height = 0;
        if(height == 0)
            height = lv_area_get_height(&cont_a) / 2;
        if(height <= 0)
            return;
        lv_obj_t *child = lv_obj_get_child(cont, 0);
        lv_area_t child_a;
        lv_obj_get_coords(child, &child_a);
        int start_row = round((-(float)child_a.y1 / height));
        int end_row = start_row + 2;

        if (force_pause) {
            // Pause all Lottie animations during scrolling for smoother performance
            for(int y = 0; y < icons_info->num; ++y)
            {
                if(icons_info->objs[y].type == ICON_TYPE_LOTTIE && icons_info->objs[y].obj)
                    lv_rlottie_set_play_mode(icons_info->objs[y].obj, LV_RLOTTIE_CTRL_PAUSE);
            }
        } else {
            // Resume only visible Lottie animations when scroll stops
            for(int y = std::max(0, start_row - 1) * 3; y < start_row * 3; ++y)
            {
                if(y < icons_info->num && icons_info->objs[y].type == ICON_TYPE_LOTTIE && icons_info->objs[y].obj)
                    lv_rlottie_set_play_mode(icons_info->objs[y].obj, LV_RLOTTIE_CTRL_PAUSE);
            }
            for(int y = start_row * 3; y < end_row * 3; ++y)
            {
                if(y < icons_info->num && icons_info->objs[y].type == ICON_TYPE_LOTTIE && icons_info->objs[y].obj)
                    lv_rlottie_set_play_mode(icons_info->objs[y].obj, LV_RLOTTIE_CTRL_LOOP);
            }
            for(int y = end_row * 3; y < std::min(icons_info->row_num, end_row + 1) * 3; ++y)
            {
                if(y < icons_info->num && icons_info->objs[y].type == ICON_TYPE_LOTTIE && icons_info->objs[y].obj)
                    lv_rlottie_set_play_mode(icons_info->objs[y].obj, LV_RLOTTIE_CTRL_PAUSE);
            }
        }
    }

    static void scroll_event_cb(lv_event_t *e)
    {
        icons_info_t *icons_info = (icons_info_t*)lv_event_get_user_data(e);
        lv_obj_t *cont = (lv_obj_t*)lv_event_get_target(e);
        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_SCROLL) {
            // During scrolling, pause all Lottie to reduce render load
            if (!g_is_scrolling) {
                g_is_scrolling = true;
                update_visible_lottie_playback(icons_info, cont, true);
            }
        } else if (code == LV_EVENT_SCROLL_END) {
            // When scroll ends, resume visible Lottie animations
            g_is_scrolling = false;
            update_visible_lottie_playback(icons_info, cont, false);
        }
    }
}


static void on_close_msg(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_msgbox_close(mbox);
}

static void on_power_msg(lv_event_t *e)
{
    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *footer = lv_msgbox_get_footer(mbox);
    lv_obj_t *reboot_btn = lv_obj_get_child(footer, 0);
    lv_obj_t *poweroff_btn = lv_obj_get_child(footer, 1);
    lv_obj_clear_flag(reboot_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(poweroff_btn, LV_OBJ_FLAG_CLICKABLE);

    if (target == reboot_btn) {
        lv_msgbox_add_title(mbox, (std::string(_("Reboot")) + "  " + LV_SYMBOL_POWER).c_str());
        lv_msgbox_add_text(mbox, _("Reboot Now"));
        sys::reboot();
    } else if (target == poweroff_btn) {
        lv_msgbox_add_title(mbox, (std::string(_("Power Off")) + "  " + LV_SYMBOL_POWER).c_str());
        lv_msgbox_add_text(mbox, _("Poweroff Now"));
        sys::poweroff();
    }
}

static void update_battery_level(lv_obj_t * /* ui_bar */)
{
    if (!pmu || !g_bar_bat_pct || !g_bar_bat_icon)
        return;
    lv_obj_t *bat = g_bar_bat_pct;
    lv_obj_t *bat_icon = g_bar_bat_icon;
    int bat_percent = pmu->get_bat_percent();
    if(bat_percent == -1) {
        log::warn("PMU: Get battery percent error");
        lv_label_set_text(bat, "0%");
        lv_label_set_text(bat_icon, LV_SYMBOL_WARNING);
    } else {
        lv_label_set_text_fmt(bat, "%d%%", bat_percent);

        if (bat_percent >= 90 && bat_percent <= 100) {
            lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_FULL);
        } else if (bat_percent >= 50 && bat_percent < 90) {
            lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_3);
        } else if (bat_percent >= 30 && bat_percent < 50) {
            lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_2);
        } else if (bat_percent >= 10 && bat_percent < 30) {
            lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_1);
        } else {
            lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_EMPTY);
        }
    }
    if(pmu->is_charging()) {
        lv_label_set_text(bat_icon, LV_SYMBOL_CHARGE);
    }
}

/** 与 gui_port/linux/img_xtool.c 中嵌入图 header.h 一致 */
static constexpr int k_bar_xtool_img_natural_h = 96;

static void sync_bar_xtool_img_scale(lv_obj_t *ui_bar)
{
    if (!g_bar_main_img || !ui_bar || !lv_obj_is_valid(g_bar_main_img))
        return;
    lv_coord_t bh = lv_obj_get_height(ui_bar);
    if (bh <= 0)
        return;
    lv_coord_t target_h = bh / 2;
    if (target_h < 1)
        target_h = 1;
    uint32_t z = (uint32_t)target_h * 256 / (uint32_t)k_bar_xtool_img_natural_h;
    if (z < 32)
        z = 32;
    lv_image_set_scale(g_bar_main_img, z);
}

void bar_update_process(void *args)
{
    bar_update_thread_exit = false;
    lv_obj_t *ui_bar = (lv_obj_t *)args;
    (void)ui_bar;

    while (!gui_destroyed) {
        if (!lv_ui_mutex_lock(200)) {
            time::sleep_ms(200);
            continue;
        }

        std::string banner;
        {
            std::lock_guard<std::mutex> lock(g_system_banner_mutex);
            banner = g_system_banner;
        }

        const bool show_bar = MAIX_LAUNCHER_ALWAYS_SHOW_TOP_BAR || !banner.empty();
        apply_top_bar_layout(show_bar);

        if (show_bar && g_bar_main_label) {
            if (!banner.empty()) {
                if (g_bar_main_img)
                    lv_obj_add_flag(g_bar_main_img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(g_bar_main_label, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_long_mode(g_bar_main_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
                if (g_bar_bat_pct)
                    lv_obj_set_width(g_bar_main_label, lv_pct(72));
                else
                    lv_obj_set_width(g_bar_main_label, lv_pct(96));
                lv_obj_align(g_bar_main_label, LV_ALIGN_LEFT_MID, lv_pct(2), 0);
                lv_label_set_text(g_bar_main_label, banner.c_str());
            } else {
                lv_obj_add_flag(g_bar_main_label, LV_OBJ_FLAG_HIDDEN);
                if (g_bar_main_img) {
                    lv_obj_clear_flag(g_bar_main_img, LV_OBJ_FLAG_HIDDEN);
                    sync_bar_xtool_img_scale(ui_bar);
                    lv_obj_center(g_bar_main_img);
                }
            }
            update_battery_level(ui_bar);
        }

        lv_ui_mutex_unlock();
        time::sleep_ms(1000);
    }

    bar_update_thread_exit = true;
}

static void item_click_event_cb(lv_event_t *e)
{
    app::APP_Info *info = (app::APP_Info*)lv_event_get_user_data(e);
    string exec_abs_path = app::get_app_path(info->id) + "/" + info->exec;
    launcher::run_app(exec_abs_path.c_str(), launcher::g_app_exec_path, info->id.c_str());
}

static void on_powerkey(int key, int state)
{
    static lv_obj_t *mbox;
    if (key == peripheral::key::Keys::KEY_POWER &&
        state == peripheral::key::State::KEY_LONG_PRESSED &&
        lv_obj_is_valid(mbox) == false)
    {
        mbox = lv_msgbox_create(lv_layer_top());
        std::string locale = i18n::get_locale();
        lv_obj_set_style_text_font(mbox, get_font_by_locale(locale), LV_PART_MAIN);
        lv_msgbox_add_title(mbox, (std::string(_("Power")) + "  " + LV_SYMBOL_POWER).c_str());
        lv_msgbox_add_close_button(mbox);
        {
            lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, _("Reboot"));
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(btn, on_power_msg, LV_EVENT_CLICKED, mbox);
        }
        {
            lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, _("Poweroff"));
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(btn, on_power_msg, LV_EVENT_CLICKED, mbox);
        }
        lv_obj_add_event_cb(mbox, on_close_msg, LV_EVENT_DELETE, mbox);
        lv_obj_center(mbox);
    } 
    else if (key == peripheral::key::Keys::KEY_POWER &&
             state == peripheral::key::State::KEY_PRESSED &&
             lv_obj_is_valid(mbox) == true)
    {
        lv_msgbox_close_async(mbox);
    }
}

void launcher::create_home_items(Maix_GUI_Activity *activity, vector<app::APP_Info> &app_info, void* screen)
{
    char buff[128] = {0};

    assert(activity->user_data1 == NULL);

    lv_obj_t *root = (lv_obj_t*)screen;

    if(app_info.size() == 0)
    {
        // create a labels to show no app
        lv_obj_t *label = lv_label_create(root);
        lv_label_set_text(label, "No app installed");
        lv_obj_center(label);
        return;
    }
    gui_destroyed = false;

    std::string locale = i18n::get_locale();
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_set_style_bg_color(root, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(root, 0, LV_PART_MAIN);


    lv_obj_t *wrapper = lv_obj_create(root);
    lv_obj_set_style_bg_color(wrapper, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_all(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(wrapper, LV_DIR_VER);
    lv_obj_clear_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(wrapper, lv_pct(100), lv_pct(100));

    int row_height_percent = 50;
    int cont_pct_y = 0;
    int cont_pct_height = 100;

    // 顶栏：所有机型仅时间/系统消息；仅 maixcam_pro 且电池可用时初始化 PMU 并显示电量
    g_bar_height_pct = 10;
    g_bar_bat_pct = nullptr;
    g_bar_bat_icon = nullptr;

    if (sys::device_id() == "maixcam_pro") {
        pmu = new ext_dev::pmu::PMU("axp2101");
        ext_dev::pmu::ChargerStatus charger_status = pmu->get_charger_status();
        const bool bat_ok = pmu->is_bat_connect() &&
            charger_status != ext_dev::pmu::ChargerStatus::CHG_TRI_STATE &&
            charger_status != ext_dev::pmu::ChargerStatus::CHG_PRE_STATE;
        if (bat_ok) {
            log::info("PMU: Battery is connect.");
        } else {
            log::info("PMU: Battery not connect.");
            delete pmu;
            pmu = nullptr;
        }
    }

    {
        lv_obj_t *ui_bar = lv_obj_create(wrapper);
        g_top_bar = ui_bar;
        lv_obj_clear_flag(ui_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(ui_bar, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
        lv_obj_set_style_border_width(ui_bar, 0, LV_PART_MAIN);
        lv_obj_set_style_margin_all(ui_bar, 0, LV_PART_MAIN);
        lv_obj_set_size(ui_bar, lv_pct(100), lv_pct(g_bar_height_pct));
        lv_obj_align(ui_bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_scrollbar_mode(ui_bar, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_radius(ui_bar, 0, 0);

        {
            lv_obj_t *label = lv_label_create(ui_bar);
            g_bar_main_label = label;
            lv_obj_set_style_text_font(label, get_font_by_locale(locale), LV_PART_MAIN);
            auto tm0 = time::localtime();
            lv_label_set_text_fmt(label, "%02d : %02d", tm0->hour, tm0->minute);
            lv_obj_center(label);

            lv_obj_t *img = lv_img_create(ui_bar);
            g_bar_main_img = img;
            lv_img_set_src(img, &img_xtool);
            lv_obj_update_layout(root);
            sync_bar_xtool_img_scale(ui_bar);
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_center(img);
        }
        if (pmu) {
            lv_obj_t *label_pct = lv_label_create(ui_bar);
            g_bar_bat_pct = label_pct;
            lv_obj_set_style_text_font(label_pct, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_set_width(label_pct, lv_pct(10));
            lv_label_set_text(label_pct, "0%");
            lv_obj_set_style_text_align(label_pct, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(label_pct, LV_ALIGN_LEFT_MID, lv_pct(83), 0);

            lv_obj_t *label_icon = lv_label_create(ui_bar);
            g_bar_bat_icon = label_icon;
            lv_obj_set_style_text_font(label_icon, &lv_font_montserrat_24, LV_PART_MAIN);
            lv_label_set_text(label_icon, LV_SYMBOL_BATTERY_EMPTY);
            lv_obj_align(label_icon, LV_ALIGN_RIGHT_MID, 0, 0);
            update_battery_level(ui_bar);
        }

#if MAIX_LAUNCHER_ALWAYS_SHOW_TOP_BAR
        cont_pct_y = g_bar_height_pct;
        cont_pct_height = 100 - g_bar_height_pct;
#else
        lv_obj_add_flag(ui_bar, LV_OBJ_FLAG_HIDDEN);
        cont_pct_y = 0;
        cont_pct_height = 100;
#endif
    }

    // init power key
    if(sys::device_id() == "maixcam_pro") {
        try {
            powerkey = new peripheral::key::Key(on_powerkey, true, "/dev/input/powerkey");
        } catch (const std::exception& e) {
            log::error("%s", e.what());
        }
    }

    // desktop icons
    lv_obj_t *cont = lv_obj_create(wrapper);
    g_desktop_cont = cont;
    if (g_top_bar) {
        bar_update_thread = new thread::Thread(bar_update_process, (void *)g_top_bar);
        bar_update_thread->detach();
        resize_monitor_thread = new thread::Thread(resize2fs_monitor_process, nullptr);
        resize_monitor_thread->detach();
    }
    lv_obj_set_style_bg_color(cont, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_all(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
    lv_obj_set_pos(cont, 0, lv_pct(cont_pct_y));
    lv_obj_set_size(cont, lv_pct(100), lv_pct(cont_pct_height));
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_style_radius(cont, LV_RADIUS_CIRCLE, 0);
    // lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    // lv_obj_set_scroll_snap_y(cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_gap(cont, 0, LV_PART_MAIN);

    icons_info_t *icons_info = (icons_info_t*)malloc(sizeof(icons_info_t));
    if(!icons_info)
    {
        printf("malloc icons_info failed\n");
    }
    activity->user_data1 = icons_info;
    icons_info->num = app_info.size();
    printf("\napp num: %d\napps:\n", icons_info->num);
    icons_info->objs = (icon_info_t*)malloc(sizeof(icon_info_t) * icons_info->num);
    if(!icons_info->objs)
    {
        free(icons_info);
        activity->user_data1 = NULL;
        icons_info = NULL;
    }
    lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL, icons_info);
    lv_obj_add_event_cb(cont, scroll_event_cb, LV_EVENT_SCROLL_END, icons_info);

    icons_info->row_num = app_info.size() / 3 + app_info.size() % 3;
    size_t count = 0;
    for(int row = 0; row < icons_info->row_num; ++row)
    {
        lv_obj_t *item_row = lv_obj_create(cont);
        lv_obj_set_width(item_row, lv_pct(100));
        lv_obj_set_height(item_row, lv_pct(row_height_percent));
        lv_obj_set_style_bg_color(item_row, theme_bg_color, LV_PART_MAIN);
        // lv_obj_set_style_border_color(item_row, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_width(item_row, 0, LV_PART_MAIN);
        lv_obj_set_style_margin_all(item_row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(item_row, 0, LV_PART_MAIN);
        // lv_obj_clear_flag(item_row, LV_OBJ_FLAG_SCROLLABLE); // this will conduct can't scroll bug
        lv_obj_set_layout(item_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        // disable item_row scrollable
        lv_obj_clear_flag(item_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_gap(item_row, 0, LV_PART_MAIN);

        for(int col = 0; col < 3; ++col)
        {
            lv_obj_t *item = lv_obj_create(item_row);
            lv_obj_set_width(item, lv_pct(33.333));
            lv_obj_set_height(item, lv_pct(100));
            lv_obj_set_style_bg_color(item, lv_color_hex(0x000000), LV_PART_MAIN);
            // lv_obj_set_style_border_color(item, lv_color_hex(0xff0000), LV_PART_MAIN);
            lv_obj_set_style_border_width(item, 0, LV_PART_MAIN);
            lv_obj_set_style_margin_all(item, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(item, lv_pct(1), LV_PART_MAIN);
            // lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE); // this will conduct can't scroll bug
            lv_obj_set_layout(item, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            // disable item scrollable
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

            if(count >= app_info.size())
                continue;

            app::APP_Info &app = app_info[count];
            printf("%ld. %s:\n", count, app.id.c_str());
            std::string path;
            if(!fs::isabs(app.icon))
                path = app::get_app_path(app.id) + "/" + app.icon;
            else
                path = app.icon;
            const char *path_c = path.c_str();

            // judge file is end up with .json or .png, json is lottie animation, png is image
            if(app.icon.substr(app.icon.length() - 4) == "json")
            {
                printf("    Lottie icon path: %s\n", path_c);
                lv_obj_t *icon = lv_rlottie_create_from_file(item, 128, 128, path_c);
                lv_rlottie_set_play_mode(icon, LV_RLOTTIE_CTRL_PAUSE);
                if(icons_info)
                {
                    icons_info->objs[count].obj = icon;
                    icons_info->objs[count].type = ICON_TYPE_LOTTIE;
                    icons_info->objs[count].row = row;
                    // printf("set icons_info->objs[i].type: %p: %d\n", icons_info->objs + valid_app_count,  icons_info->objs[valid_app_count].type);
                }
            }
            else if(app.icon.substr(app.icon.length() - 4) == ".png")
            {
                lv_obj_t *icon = lv_img_create(item);
                snprintf(buff, sizeof(buff), "%c:%s", LV_FS_STDIO_LETTER, path_c);
                printf("    PNG icon path: %s\n", buff);
                lv_img_set_src(icon, buff);
                lv_obj_set_width(icon, 128);
                lv_obj_set_height(icon, 128);
                if(icons_info)
                {
                    icons_info->objs[count].obj = icon;
                    icons_info->objs[count].type = ICON_TYPE_PNG;
                    icons_info->objs[count].row = row;
                }
            }
            else if(app.icon.substr(app.icon.length() - 4) == ".gif")
            {
                // LV_IMAGE_DECLARE(img_bulb_gif);
                lv_obj_t *icon = lv_gif_create(item);
                snprintf(buff, sizeof(buff), "%c:%s", LV_FS_STDIO_LETTER, path_c);
                printf("    GIF icon path: %s\n", buff);
                lv_gif_set_src(icon, buff);
                lv_obj_set_width(icon, 128);
                lv_obj_set_height(icon, 128);
                if(icons_info)
                {
                    icons_info->objs[count].obj = icon;
                    icons_info->objs[count].type = ICON_TYPE_GIF;
                    icons_info->objs[count].row = row;
                }
            }
            else
            {
                if(icons_info)
                {
                    icons_info->objs[count].obj = NULL;
                    icons_info->objs[count].type = ICON_TYPE_NONE;
                    icons_info->objs[count].row = -1;
                }
                printf("icon file format not support, only surpport .json and .png\n");
            }

            // label
            lv_obj_t *label = lv_label_create(item);
            if(app.names.find(locale) != app.names.end())
                lv_label_set_text_fmt(label, app.names[locale].c_str());
            else
                lv_label_set_text_fmt(label, app.name.c_str());
            lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
            lv_obj_set_style_pad_all(label, 1, LV_PART_MAIN);

            lv_obj_add_event_cb(item, item_click_event_cb, LV_EVENT_CLICKED, (void*)&app);

            ++count;
        }
    }

    /*Update the buttons position manually for first*/
    lv_obj_send_event(cont, LV_EVENT_SCROLL, NULL);

    /*Be sure the fist button is in the middle*/
    lv_obj_scroll_to_view(lv_obj_get_child(cont, 0), LV_ANIM_OFF);

    tuple<string, err::Err, string> app_exit_msg = app::get_exit_msg();
    // if err not ERR_NONE, show error msg
    if(get<1>(app_exit_msg) != err::ERR_NONE)
    {
        std::string msg = get<2>(app_exit_msg);
        log::info("Got last app exit msg: %s\n", msg.c_str());
        static const char *btns[] = {"Yes", ""};
        lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
        lv_msgbox_add_title(mbox, ("APP " + get<0>(app_exit_msg) + " Error").c_str());
        lv_msgbox_add_text(mbox, msg.c_str());
        lv_msgbox_add_close_button(mbox);
        lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, btns[0]);
        lv_obj_add_event_cb(btn, on_close_msg, LV_EVENT_CLICKED, mbox);
        lv_obj_add_event_cb(mbox, on_close_msg, LV_EVENT_DELETE, mbox);
        lv_obj_center(mbox);
        // delete exit msg
        #define APP_ROOT_PATH "/maixapp"
        string path = string(APP_ROOT_PATH "/tmp/app_exit_msg.txt");
        fs::remove(path.c_str());
    }

}


void launcher::home_items_free(Maix_GUI_Activity *activity, vector<app::APP_Info>& app_info, void* screen)
{
    gui_destroyed = true;
    log::info("wait bar update thread exit");
    while(!bar_update_thread_exit)
    {
        time::sleep_ms(20);
    }
    log::info("wait bar update thread exit done");

    if (bar_update_thread != nullptr) {
        delete bar_update_thread;
        bar_update_thread = nullptr;
    }

    log::info("wait resize monitor thread exit");
    while (!resize_monitor_thread_exit) {
        time::sleep_ms(20);
    }
    log::info("wait resize monitor thread exit done");
    if (resize_monitor_thread != nullptr) {
        delete resize_monitor_thread;
        resize_monitor_thread = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(g_system_banner_mutex);
        g_system_banner.clear();
    }
    g_top_bar = nullptr;
    g_bar_main_label = nullptr;
    g_bar_main_img = nullptr;
    g_bar_bat_pct = nullptr;
    g_bar_bat_icon = nullptr;
    g_desktop_cont = nullptr;

    if (pmu != nullptr) {
        delete pmu;
        pmu = nullptr;
    }

    if (powerkey != nullptr) {
        delete powerkey;
        powerkey = nullptr;
    }

    if(activity->user_data1)
    {
        icons_info_t *icons_info = (icons_info_t*)activity->user_data1;
        free(icons_info->objs);
        free(icons_info);
        activity->user_data1 = NULL;
    }
}

