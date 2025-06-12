#include "main.hpp"
#include "lvgl.h"
#include "maix_thread.hpp"
#include <string>
#include "maix_app.hpp"
#include "maix_gui.hpp"
#include "maix_camera.hpp"
#include "maix_time.hpp"
#include "ui.h"
#include "maix_wifi.hpp"
#include "maix_basic.hpp"

using namespace maix::network;

typedef struct
{
    thread::Thread *thread;
    bool run;
    bool exit_ok;
    void *img_buff;
    lv_obj_t *canvas;
    int width;
    int height;
    Maix_GUI_Activity *activity;
} thread_args_t;

static lv_obj_t *label_ip = nullptr;
static lv_obj_t *label_ssid = nullptr;
static lv_obj_t *scan_btn = nullptr;
static lv_obj_t *ap_list = nullptr;
static lv_obj_t *passwd = nullptr;
static lv_obj_t *connect_label = nullptr;
static lv_obj_t *connect_btn = nullptr;
static lv_obj_t *qr_scan_label = nullptr;
static thread::Thread *scan_thread = nullptr;
static bool scan_thread_should_exit = false;
static bool scan_thread_exit = false;
static std::vector<wifi::AP_Info> valid_aps;
static wifi::Wifi *wifi_obj = nullptr;
static std::string ap_ssid = "";
static std::string ap_passwd = "";
static thread_args_t *args = nullptr;
static bool scanning_qr = false;

static bool parseJsonStyle(const std::string &text, std::string &ap_ssid, std::string &ap_passwd)
{
    size_t start, end;
    bool ok = false;

    start = text.find("\"s\":\"");
    if (start != std::string::npos)
    {
        start += 5; // Skip over "\"s\":\""
        end = text.find("\"", start);
        if (end != std::string::npos)
        {
            ap_ssid = text.substr(start, end - start);
            ok = true;
        }
    }

    start = text.find("\"p\":\"");
    if (start != std::string::npos)
    {
        start += 5; // Skip over "\"p\":\""
        end = text.find("\"", start);
        if (end != std::string::npos)
        {
            ap_passwd = text.substr(start, end - start);
            ok = true;
        }
    }
    return ok;
}

static bool parseWifiString(const std::string &text, std::string &ap_ssid, std::string &ap_passwd)
{
    size_t start, end;
    bool ok = false;

    start = text.find("S:");
    if (start != std::string::npos)
    {
        start += 2; // Skip over "S:"
        end = text.find(";", start);
        if (end != std::string::npos)
        {
            ap_ssid = text.substr(start, end - start);
            ok = true;
        }
    }

    start = text.find("P:");
    if (start != std::string::npos)
    {
        start += 2; // Skip over "P:"
        end = text.find(";", start);
        if (end != std::string::npos)
        {
            ap_passwd = text.substr(start, end - start);
            ok = true;
        }
    }
    return ok;
}

static void on_close_msg(lv_event_t *e)
{
    lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_msgbox_close(mbox);
    showing_msg = false;
}

static void show_msg_with_lock(const string &title, const string &msg)
{
    showing_msg = true;
    lv_ui_mutex_lock(-1);
    std::string locale = i18n::get_locale();
    lv_obj_set_style_text_font(lv_scr_act(), get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
    lv_msgbox_add_title(mbox, title.c_str());
    lv_msgbox_add_text(mbox, msg.c_str());
    lv_msgbox_add_close_button(mbox);
    lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, _("OK"));
    lv_obj_add_event_cb(btn, on_close_msg, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
    lv_ui_mutex_unlock();
}

static void show_msg(const string &title, const string &msg)
{
    showing_msg = true;
    std::string locale = i18n::get_locale();
    lv_obj_set_style_text_font(lv_scr_act(), get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
    lv_msgbox_add_close_button(mbox);
    lv_msgbox_add_title(mbox, title.c_str());
    lv_msgbox_add_text(mbox, msg.c_str());
    lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, _("OK"));
    lv_obj_add_event_cb(btn, on_close_msg, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
}

static void camera_scan_process(void *args_in)
{
    err::Err e;
    thread_args_t *args = (thread_args_t *)args_in;
    camera::Camera *cam = nullptr;
    try
    {
        cam = new camera::Camera(-1, -1, image::FMT_RGB888, nullptr, -1, 3, false);
        log::info("start camera");
        e = cam->open(args->width, args->height, image::FMT_BGRA8888);
    }
    catch (const std::exception &err)
    {
        e = err::ERR_NOT_FOUND;
    }
    if (e != err::ERR_NONE)
    {
        log::error("camera start failed: %d", e);
        show_msg_with_lock("Error", "Camera start failed");
        args->run = false;
        args->exit_ok = true;
        return;
    }
    log::info("start camera ok");
    while (args->run)
    {
        if (!scanning_qr)
        {
            time::sleep_ms(10);
            continue;
        }
        // read image from camera
        // decode image
        // scan QR code from image to get url or json object
        image::Image *img0 = cam->read();
        if (!img0)
        {
            printf("camera read failed\n");
            if (!args->run) // to avoid block when on_qr_gui_destroy is called and wait for this thread exit
            {
                show_msg("Error", "Camera read failed");
                break;
            }
            show_msg_with_lock("Error", "Camera read failed");
            break;
        }
        if (!args->run) // to avoid block when on_qr_gui_destroy is called and wait for this thread exit
        {
            delete img0;
            break;
        }
        while (!lv_ui_mutex_lock(100))
        {
            if (!args->run) // to avoid block when on_qr_gui_destroy is called and wait for this thread exit
            {
                delete img0;
                break;
            }
        }
        if (!args->run)
            break;
        memcpy((uint8_t *)args->img_buff, img0->data(), img0->data_size());
        // lv_canvas_set_buffer(args->canvas, buff, args->width, args->height, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_invalidate(args->canvas);
        lv_ui_mutex_unlock();
        // scan QR code
        // ZXing::ImageView img = ZXing::ImageView(buff, args->width, args->height, ZXing::ImageFormat::BGRX);
        std::vector<image::QRCode> result = img0->find_qrcodes();
        delete img0;
        for (auto info : result)
        {
            std::string text = info.payload();
            std::string ssid, passwd;
            if (text.length() > 0)
            {
                log::info("qr code content: %s", text.c_str());
                if (text[0] == '{') // {"s":"211","p":"111"}
                {
                    if (!parseJsonStyle(text, ssid, passwd))
                    {
                        log::warn("decode json AP info failed");
                        continue;
                    }
                }
                else if (text.substr(0, 4) == "WIFI") // WIFI:S:Neucrack Guest;T:WPA;P:Neucrack;H:false;;
                {
                    if (!parseWifiString(text, ssid, passwd))
                    {
                        log::warn("decode WIFI AP info failed");
                        continue;
                    }
                }
                if (!ssid.empty())
                {
                    log::info("SSID: %s, Passwd: %s", ssid.c_str(), passwd.c_str());
                    ap_passwd = passwd;
                    ap_ssid = ssid;
                }
                else
                {
                    log::warn("qrcode format not valid");
                    continue;
                }
                scanning_qr = false;
                while (!lv_ui_mutex_lock(100))
                {
                    time::sleep_ms(1);
                    if (!args->run) // to avoid block when on_qr_gui_destroy is called and wait for this thread exit
                    {
                        break;
                    }
                }
                if (!args->run)
                    break;
                lv_obj_add_flag(args->canvas, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(qr_scan_label, LV_OBJ_FLAG_HIDDEN);
                lv_ui_mutex_unlock();
            }
        }
    }
    args->exit_ok = true;
    if (cam)
        delete cam;
    printf("camera thread exit ok\n");
}

void scan_ap_thread(void *args)
{
    scan_thread_exit = false;
    std::vector<std::string> wifi_ifaces = wifi::list_devices();
    if (wifi_ifaces.empty())
    {
        log::error("no wifi iface found");
        lv_ui_mutex_lock(-1);
        lv_label_set_text(label_ip, _("No WiFi card"));
        lv_ui_mutex_unlock();
        scan_thread_exit = true;
        return;
    }

    std::vector<wifi::AP_Info> aps;
    log::info("wifi iface: %s", wifi_ifaces[0].c_str());
    wifi_obj = new wifi::Wifi(wifi_ifaces[0]);
    wifi_obj->start_scan();
    while (!scan_thread_should_exit)
    {
        if (!ap_ssid.empty())
        {
            time::sleep_ms(1); // not use memory barrier, for easy we sleep here to wait ap_passwd set
            err::Err error = wifi_obj->connect(ap_ssid, ap_passwd, false);
            if (error != err::Err::ERR_NONE)
            {
                while (!lv_ui_mutex_lock(100))
                {
                    time::sleep_ms(1);
                    if (scan_thread_should_exit)
                        break;
                }
                if (scan_thread_should_exit)
                    break;
                show_msg("Error", (std::string(_("Connect failed")) + ": " + err::to_str(error)).c_str());
                lv_ui_mutex_unlock();
            }
            else
            {
                while (!lv_ui_mutex_lock(100))
                {
                    time::sleep_ms(1);
                    if (scan_thread_should_exit)
                        break;
                }
                if (scan_thread_should_exit)
                    break;
                show_msg("Info", _("Set success, please wait for got IP, if long time no IP, check password"));
                lv_ui_mutex_unlock();
                log::info("Wifi connected! Sync time...");
                ::system("sync_time -b &");
            }
            std::string ssid = ap_ssid;
            ap_ssid = "";
            ap_passwd = "";
            while (!lv_ui_mutex_lock(100))
            {
                time::sleep_ms(1);
                if (scan_thread_should_exit)
                    break;
            }
            if (scan_thread_should_exit)
                break;
            lv_label_set_text(connect_label, _("Connect"));
            lv_obj_add_flag(connect_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_state(connect_btn, LV_STATE_DISABLED, false);
            lv_label_set_text(label_ssid, _("SSID: " + ssid));
            lv_ui_mutex_unlock();
            wifi_obj->start_scan();
        }
        if (wifi_obj->is_connected())
        {
            std::string ip = wifi_obj->get_ip();
            if (ip.length() > 0)
            {
                while (!lv_ui_mutex_lock(100))
                {
                    time::sleep_ms(1);
                    if (scan_thread_should_exit)
                        break;
                }
                if (scan_thread_should_exit)
                    break;
                lv_label_set_text(label_ip, ("IP: " + ip).c_str());
                lv_ui_mutex_unlock();
            }
        }
        else
        {
            while (!lv_ui_mutex_lock(100))
            {
                time::sleep_ms(1);
                if (scan_thread_should_exit)
                    break;
            }
            if (scan_thread_should_exit)
                break;
            lv_label_set_text(label_ip, _("No IP"));
            lv_ui_mutex_unlock();
        }
        time::sleep_ms(200);
    }
    wifi_obj->stop_scan();
    scan_thread_exit = true;
}

void on_wifi_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args_in)
{
    std::string locale = i18n::get_locale();

    args = new thread_args_t;
    lv_obj_t *root = (lv_obj_t *)obj;
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_set_style_bg_color(root, theme_bg_color, LV_PART_MAIN);

    // layout
    lv_obj_t *layout_col = lv_obj_create(root);
    lv_obj_set_flex_flow(layout_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(layout_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(layout_col, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(layout_col, 0, LV_PART_MAIN);
    lv_obj_set_size(layout_col, lv_pct(100), lv_pct(100));

    // current wlan ip
    label_ip = lv_label_create(layout_col);
    lv_label_set_text(label_ip, _("No IP"));
    label_ssid = lv_label_create(layout_col);
    fs::File *f = fs::open("/boot/wifi.ssid", "r");
    if (f)
    {
        std::string *ssid = f->readline();
        lv_label_set_text(label_ssid, _("SSID: " + *ssid));
        delete ssid;
        delete f;
    }
    else
    {
        lv_label_set_text(label_ssid, _("SSID: "));
    }

    // camera
    // get screen size
    args->width = 320;
    args->height = 240;
    args->thread = nullptr;

    args->img_buff = malloc(args->width * args->height * sizeof(lv_color32_t));
    if (!args->img_buff)
    {
        log::error("malloc failed\n");
        show_msg("Error", "Malloc failed");
    }
    else
    {
        log::debug("-- cam buff: %p, %d x %d x %ld = %ld\n", args->img_buff, args->width, args->height, sizeof(lv_color32_t), args->width * args->height * sizeof(lv_color32_t));
        qr_scan_label = lv_label_create(layout_col);
        lv_label_set_text(qr_scan_label, (std::string(_("QR shared by phone"))).c_str());
        lv_obj_t *canvas = lv_canvas_create(layout_col);
        lv_canvas_set_buffer(canvas, args->img_buff, args->width, args->height, LV_COLOR_FORMAT_ARGB8888);
        args->canvas = canvas;
        lv_obj_add_flag(args->canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(qr_scan_label, LV_OBJ_FLAG_HIDDEN);

        // anime
        string path = app::get_app_path() + "/assets/scan.json";

        lv_obj_t *container = lv_label_create(canvas);
        lv_label_set_text(container, "");
        lv_obj_set_size(container, 240, 240);
        lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
        lv_rlottie_create_from_file(container, 240, 240, path.c_str());

        args->run = true;
        args->activity = activity;
        activity->user_data1 = args;
    }
    args->exit_ok = true;

    // scan button row
    lv_obj_t *layout_row0 = lv_obj_create(layout_col);
    lv_obj_set_flex_flow(layout_row0, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_row0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(layout_row0, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(layout_row0, 0, LV_PART_MAIN);
    lv_obj_clear_flag(layout_row0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(layout_row0, lv_pct(100), lv_pct(20));
    // Scan qrcode
    lv_obj_t *qr_btn = lv_btn_create(layout_row0);
    lv_obj_t *label_qr_btn = lv_label_create(qr_btn);
    lv_label_set_text(label_qr_btn, _("Scan QR"));
    // add event
    lv_obj_add_event_cb(qr_btn, [](lv_event_t *e)
                        {
                            if(args && args->canvas)
                            {
                                lv_obj_remove_flag(args->canvas, LV_OBJ_FLAG_HIDDEN);
                                lv_obj_remove_flag(qr_scan_label, LV_OBJ_FLAG_HIDDEN);
                                scanning_qr = true;
                                if(!args->thread)
                                {
                                    args->exit_ok = false;
                                    thread::Thread *thread = new thread::Thread(camera_scan_process, args);
                                    args->thread = thread;
                                    thread->detach();
                                }
                            } }, LV_EVENT_CLICKED, NULL);

    // ap list
    ap_list = lv_dropdown_create(layout_row0);
    lv_dropdown_set_options(ap_list, "");

    // scanning text
    scan_btn = lv_btn_create(layout_row0);
    lv_obj_t *label_scan = lv_label_create(scan_btn);
    lv_label_set_text(label_scan, _("Scan"));
    // add event
    lv_obj_add_event_cb(scan_btn, [](lv_event_t *e)
                        {
        if(!wifi_obj)
        {
            show_msg("Error", _("Try again"));
            return;
        }
        std::vector<wifi::AP_Info> all_aps = wifi_obj->get_scan_result();
        valid_aps.clear();
        for (auto &ap : all_aps)
        {
            if (ap.ssid.size() > 0)
            {
                valid_aps.push_back(ap);
            }
        }
        // sort by rssi, from high to low
        std::sort(valid_aps.begin(), valid_aps.end(), [](const wifi::AP_Info &a, const wifi::AP_Info &b) {
            return a.rssi > b.rssi;
        });
        std::string aps_str = "";
        for (auto &ap : valid_aps)
        {
            std::string text = "[" + std::to_string(ap.rssi) + "] " + ap.ssid_str() + " [ch" + std::to_string(ap.channel) + "]";
            aps_str += text + "\n";
        }
        lv_dropdown_set_options(ap_list, aps_str.c_str()); }, LV_EVENT_CLICKED, NULL);

    // passwd and btn at on line with flex row layout
    lv_obj_t *layout_row = lv_obj_create(layout_col);
    lv_obj_set_flex_flow(layout_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(layout_row, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(layout_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(layout_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(layout_row, 0, LV_PART_MAIN);
    lv_obj_set_size(layout_row, lv_pct(100), lv_pct(20));
    lv_obj_clear_flag(layout_row, LV_OBJ_FLAG_SCROLLABLE);

    // password input
    passwd = lv_textarea_create(layout_row);
    lv_textarea_set_placeholder_text(passwd, _("Password"));
    lv_textarea_set_one_line(passwd, true);
    lv_obj_set_style_border_width(passwd, 0, LV_PART_MAIN);
    lv_obj_set_size(passwd, lv_pct(50), lv_pct(100));
    lv_obj_align(passwd, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_state(passwd, LV_STATE_FOCUSED);
    lv_obj_clear_flag(passwd, LV_OBJ_FLAG_SCROLLABLE);
    // when click passwd, scroll to end of layout_col
    lv_obj_add_event_cb(passwd, [](lv_event_t *e)
                        {
        lv_obj_t *layout_col = (lv_obj_t *)lv_event_get_user_data(e);
        int32_t height = lv_obj_get_height(layout_col);
        if (e->code == LV_EVENT_FOCUSED)
        {
            lv_obj_scroll_to_y(layout_col, height, LV_ANIM_ON);
        } }, LV_EVENT_FOCUSED, layout_col);

    // button
    connect_btn = lv_btn_create(layout_row);
    connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, _("Connect"));
    // add event
    lv_obj_add_event_cb(connect_btn, [](lv_event_t *e)
                        {
        lv_obj_remove_flag(connect_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_state(connect_btn, LV_STATE_DISABLED, true);
        // get selected ap
        uint16_t idx = lv_dropdown_get_selected(ap_list);
        if (idx >= valid_aps.size())
        {
            show_msg("Error", _("Please select a AP"));
            lv_obj_add_flag(connect_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_state(connect_btn, LV_STATE_DISABLED, false);
            return;
        }
        wifi::AP_Info ap = valid_aps[idx];
        // get password
        const char *passwd_str = lv_textarea_get_text(passwd);
        if (strlen(passwd_str) == 0)
        {
            log::warn("not set wifi password");
            passwd_str = "";
        }
        // connect to ap
        ap_ssid = ap.ssid_str();
        ap_passwd = passwd_str;
        log::info("connect to %s", ap_ssid.c_str());
        lv_label_set_text(connect_label, _("Connecting...")); }, LV_EVENT_CLICKED, NULL);

    // keyboard
    lv_obj_t *kb = lv_keyboard_create(layout_col);
    lv_obj_set_size(kb, lv_pct(100), lv_pct(75));
    lv_keyboard_set_textarea(kb, passwd);

    // scan thread
    scan_thread_should_exit = false;
    scan_thread = new thread::Thread(scan_ap_thread, NULL);
    scan_thread->detach();
}

void on_wifi_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args_in)
{
    thread_args_t *args = (thread_args_t *)activity->user_data1;
    if (args->run && !args->exit_ok)
    {
        args->run = false;
        log::info("wait qr scan thread exit\n");
        while (!args->exit_ok)
        {
            usleep(100000);
        }
        log::info("wait qr scan thread exit ok\n");
    }
    delete args->thread;
    if (args->img_buff)
    {
        free(args->img_buff);
        args->img_buff = NULL;
    }
    lv_obj_del(args->canvas);
    delete args;

    if (scan_thread && !scan_thread_exit)
    {
        scan_thread_should_exit = true;
        log::info("wait ap scan thread exit");
        while (!scan_thread_exit)
        {
            time::sleep_ms(50);
        }
        log::info("wait ap scan thread exit done");
        delete scan_thread;
        scan_thread = nullptr;
    }
    if (wifi_obj)
    {
        delete wifi_obj;
        wifi_obj = nullptr;
    }
}
