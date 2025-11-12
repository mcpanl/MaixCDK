
#include "global_config.h"
#include "global_build_info_time.h"
#include "global_build_info_version.h"

/**
 * @file main
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "maix_basic.hpp"
#include "main.hpp"
#include "lvgl.h"
#include "rlottie/lv_rlottie.h"
#include <assert.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "maix_gui.hpp"
#include "maix_app.hpp"
#include "maix_util.hpp"
#include "maix_fs.hpp"
#include "ui.h"
#include <thread>
#include "maix_comm.hpp"


bool showing_msg = false;
display::Display *p_display = NULL;

static Maix_GUI_Activity *main_activity = NULL, *info_activity = NULL, *language_activity = NULL, *wifi_activity = NULL;
static Maix_GUI_Activity *comm_activity = NULL, *about_activity = NULL, *poweroff_activity = NULL, *volume_activity = NULL;
static Maix_GUI_Activity *upgrade_maixpy = NULL, *upgrade_lib_activity = NULL, *auto_start_activity = NULL, *backlight_activity = NULL, *usb_settings = NULL;
static Maix_GUI_Activity *datetime = NULL;
static std::vector<Maix_GUI_Activity *> settings_activities;
static const std::vector<string> icons = {"info.png", "wifi.png", "install_lib.png", "auto_start.png", "backlight.png", "volume.png", "language.png", "datetime.png", "usb.png", "about.png", "power.png"};
static const std::vector<std::string> names = {"Device Info", "WiFi", "Install Runtime", "Auto Start", "Backlight", "Volume", "Language", "Datetime", "USB Settings", "Privacy and Usage", "Power"};


lv_obj_t *create_img_item(lv_obj_t *parent, string path, int w, int h)
{
    char buff[128] = {0};
    const char *path_c = path.c_str();
    lv_obj_t *icon = NULL;
    // judge file is end up with .json or .png, json is lottie animation, png is image
    if (path.substr(path.length() - 4) == "json")
    {
        printf("-- Lottie icon path: %s\n", path_c);
        icon = lv_rlottie_create_from_file(parent, w, h, path_c);
    }
    else if (path.substr(path.length() - 4) == ".png")
    {
        icon = lv_img_create(parent);
        snprintf(buff, sizeof(buff), "%c:%s", LV_FS_STDIO_LETTER, path_c);
        printf("-- PNG icon path: %s\n", buff);
        lv_img_set_src(icon, buff);
    }
    else
    {
        printf("icon file format not support, only surpport .json and .png\n");
        return NULL;
    }
    return icon;
}

static void btn_event_cb(lv_event_t *e)
{
    uint32_t code = lv_event_get_code(e);
    long i = (long)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED)
    {
        if(settings_activities.size() > (size_t)i)
            settings_activities[i]->active();
    }
}

/**
 * Create a button with a label and react on click event.
 */
static void main_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    assert(names.size() == icons.size());

    printf("\n\n------ on main_gui\n");

    std::string locale = i18n::get_locale();

    lv_obj_t *root = (lv_obj_t *)obj;
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_set_style_bg_color(root, theme_bg_color, LV_PART_MAIN);
    lv_obj_t *layout_col = lv_obj_create(root);
    lv_obj_set_flex_flow(layout_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(layout_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(layout_col, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(layout_col, 0, LV_PART_MAIN);
    lv_obj_set_size(layout_col, lv_pct(100), lv_pct(100));

    lv_obj_t *title_bar = lv_obj_create(layout_col);
    lv_obj_set_size(title_bar, lv_pct(100), lv_pct(10));
    lv_obj_set_style_bg_color(title_bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);
    lv_obj_set_layout(title_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(title_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Settings");
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, get_font_by_locale(locale), LV_PART_MAIN);

    for (size_t i = 0; i < names.size(); ++i)
    {
        lv_obj_t *btn = lv_btn_create(layout_col);
        lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(btn, theme_btn_color, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_size(btn, lv_pct(70), lv_pct(30));
        lv_obj_set_style_radius(btn, lv_pct(100), LV_PART_MAIN);
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void*)i);
        // icon
        std::string path = fs::abspath(app::get_app_path()) + "/assets/" + icons[i];
        // a transparent container
        lv_obj_t *icon_container = lv_obj_create(btn);
        lv_obj_set_style_bg_opa(icon_container, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(icon_container, 0, LV_PART_MAIN);
        create_img_item(icon_container, path, 64, 64);
        lv_obj_set_layout(icon_container, LV_LAYOUT_FLEX);
        lv_obj_set_flex_align(icon_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(icon_container, btn_event_cb, LV_EVENT_CLICKED, (void*)i);

        // name
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, _(names[i]));
        lv_obj_set_style_text_color(label, theme_text_color, LV_PART_MAIN);
        lv_obj_set_flex_grow(icon_container, 1);
        lv_obj_set_flex_grow(label, 2);
        // text center
        lv_obj_set_style_text_align(btn, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }
    lv_obj_scroll_to_view(lv_obj_get_child(layout_col, 0), LV_ANIM_OFF);
}

#define BUFF_RX_LEN 1024

int _main(int argc, char **argv)
{
    err::Err e;
    int ret;

    std::thread([](){
        protocol::MSG *msg;
        comm::CommProtocol p = comm::CommProtocol(BUFF_RX_LEN);
        if(p.valid())
        {
            while(!app::need_exit())
            {
                // static uint8_t buff_rx[] = { 0xAA, 0xCA, 0xAC, 0xBB, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0xC8, 0xF5 };
                msg = p.get_msg();
                if(msg)
                {
                    log::info("Get MSG");
                    if(msg->is_resp)
                        continue;
                    if (!msg->has_been_replied) {
                        auto resp_ret = p.resp_err(msg->cmd, err::Err::ERR_ARGS, "Unsupport CMD body");
                        if (resp_ret != err::Err::ERR_NONE) {
                            log::error("[%s:%d] resp_ok failed, code = %u",
                                __PRETTY_FUNCTION__, __LINE__, (uint8_t)resp_ret);
                        }
                        msg->has_been_replied = true;
                    }
                    delete msg;
                }

            }
        }
    }).detach();

    // init display
    display::Display screen = display::Display();
    p_display = &screen;
    e = screen.open();
    err::check_raise(e, "display open failed");

    // touch screen
    touchscreen::TouchScreen touchscreen = touchscreen::TouchScreen();
    e = touchscreen.open();
    err::check_raise(e, "touchscreen open failed");

    Maix_GUI<MAIX_GUI_OBJ_T> gui(true);
    ret = gui.init(&screen, &touchscreen);
    if (ret != 0)
    {
        log::error("gui init failed\n");
        return 1;
    }

    main_activity = gui.get_main_activity();
    main_activity->set_init_ui_cb(main_gui);
    // main_activity->set_destroy_cb(on_destroy_gui);
    main_activity->active();

    info_activity = new Maix_GUI_Activity(main_activity, false, on_iofo_gui, NULL, NULL, NULL, on_iofo_gui_destroy, NULL, "Device Info");
    language_activity = new Maix_GUI_Activity(main_activity, false, on_language_gui, NULL, NULL, NULL, on_language_gui_destroy, NULL, "Language");
    wifi_activity = new Maix_GUI_Activity(main_activity, false, on_wifi_gui, NULL, NULL, NULL, on_wifi_gui_destroy, NULL, "WiFi");
    // comm_activity = new Maix_GUI_Activity(main_activity, false, on_comm_gui, NULL, NULL, NULL, on_comm_gui_destroy, NULL, "Communicate");
    // upgrade_maixpy = new Maix_GUI_Activity(main_activity, false, on_upgrade_maixpy_gui, NULL, NULL, NULL, on_upgrade_maixpy_gui_destroy, NULL, "Upgrade MaixPy");
    usb_settings = new Maix_GUI_Activity(main_activity, false, on_usb_settings_gui, NULL, NULL, NULL, on_usb_settings_gui_destroy, NULL, "USB Settings");
    datetime = new Maix_GUI_Activity(main_activity, false, on_datetime_gui, NULL, NULL, NULL, on_datetime_gui_destroy, NULL, "Datetime Settings");
    about_activity = new Maix_GUI_Activity(main_activity, false, on_about_gui, NULL, NULL, NULL, on_about_gui_destroy, NULL, "About");
    poweroff_activity = new Maix_GUI_Activity(main_activity, false, on_poweroff_gui, NULL, NULL, NULL, on_poweroff_gui_destroy, NULL, "Power Off");
    upgrade_lib_activity = new Maix_GUI_Activity(main_activity, false, on_upgrade_lib_gui, NULL, NULL, NULL, on_upgrade_lib_gui_destroy, NULL, "Upgrade lib");
    auto_start_activity = new Maix_GUI_Activity(main_activity, false, on_auto_start_gui, NULL, NULL, NULL, on_auto_start_gui_destroy, NULL, "Auto start");
    backlight_activity = new Maix_GUI_Activity(main_activity, false, on_backlight_gui, NULL, NULL, NULL, on_backlight_gui_destroy, NULL, "Backlight");
    volume_activity = new Maix_GUI_Activity(main_activity, false, on_volume_gui, NULL, NULL, NULL, on_volume_gui_destroy, NULL, "Volume");

    settings_activities.push_back(info_activity);
    settings_activities.push_back(wifi_activity);
    settings_activities.push_back(upgrade_lib_activity);
    // settings_activities.push_back(upgrade_maixpy);
    settings_activities.push_back(auto_start_activity);
    settings_activities.push_back(backlight_activity);
    settings_activities.push_back(volume_activity);
    settings_activities.push_back(language_activity);
    settings_activities.push_back(datetime);
    settings_activities.push_back(usb_settings);
    // settings_activities.push_back(comm_activity);
    settings_activities.push_back(about_activity);
    settings_activities.push_back(poweroff_activity);

    gui.run();

    return 0;
}

int main(int argc, char* argv[])
{
    if(argc > 1 && strstr(argv[1], "install_runtime") != NULL)
    {
        return install_runtime();
    }
    // Catch signal and process
    sys::register_default_signal_handle();

    // Use CATCH_EXCEPTION_RUN_RETURN to catch exception,
    // if we don't catch exception, when program throw exception, the objects will not be destructed.
    // So we catch exception here to let resources be released(call objects' destructor) before exit.
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
