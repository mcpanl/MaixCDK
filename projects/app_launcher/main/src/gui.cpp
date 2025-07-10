#include "gui.hpp"
#include "lvgl.h"
#include "rlottie/lv_rlottie.h"
#include "run_app.hpp"
#include "maix_app.hpp"
#include "maix_basic.hpp"
#include "i18n.hpp"
#include "maix_network.hpp"
#include "httplib.h"
#include "maix_pmu.hpp"
#include "maix_key.hpp"

using namespace maix;

static thread::Thread *check_thread = nullptr;
static thread::Thread *bar_update_thread = nullptr;
static ext_dev::pmu::PMU *pmu = nullptr;
static peripheral::key::Key *powerkey = nullptr;
static bool gui_destroyed = true;
static bool check_thread_exit = true;
static bool bar_update_thread_exit = true;

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
    static void scroll_event_cb(lv_event_t *e)
    {
        icons_info_t *icons_info = (icons_info_t*)lv_event_get_user_data(e);
        lv_obj_t *cont = (lv_obj_t*)lv_event_get_target(e);
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
        int start_row = round((-(float)child_a.y1 / height)); // + lv_area_get_height(&child_a) - 5;
        int end_row = start_row + 2;
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


// Split a string by delimiter
static std::vector<std::string> split(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

static std::string get_curr_version()
{
    #define LIB_VERSION_FILE_PATH "/maixapp/maixcam_lib.version"
    fs::File *file = fs::open(LIB_VERSION_FILE_PATH, "r");
    if (!file)
    {
        return "";
    }
    std::string *version = file->readline();
    std::string curr_version = *version;
    delete version;
    file->close();
    delete file;
    return curr_version;
}

static std::string parse_version(const std::string &response)
{
    log::info("%s", response.c_str());
    // Find the position of version key in the response string
    size_t version_pos = response.find("version");

    // If version key is found
    if (version_pos != std::string::npos)
    {
        // Find the start and end positions of the version value
        size_t start_pos = response.find(":", version_pos + 1) + 1;
        size_t end_pos = response.find("\"", start_pos + 1);

        // Extract the version substring
        if (start_pos != std::string::npos && end_pos != std::string::npos)
        {
            return response.substr(start_pos + 1, end_pos - start_pos - 1);
        }
    }
    // Return empty string if version key is not found or if extraction fails
    return "";
}

static std::string get_ip_by_hostname(const std::string &hostname) {
    struct addrinfo hints, *res, *p;
    int status;
    char ipstr[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
    hints.ai_socktype = SOCK_STREAM;

    if ((status = getaddrinfo(hostname.c_str(), NULL, &hints, &res)) != 0) {
        log::error("getaddrinfo: %s", gai_strerror(status));
        return "";
    }

    for (p = res; p != NULL; p = p->ai_next) {
        void *addr;
        std::string ipver;

        // get the pointer to the address itself,
        // different fields in IPv4 and IPv6:
        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
        }

        // convert the IP to a string and return it
        inet_ntop(p->ai_family, addr, ipstr, sizeof ipstr);
        freeaddrinfo(res); // free the linked list
        return ipstr; // return the first IP address found
    }

    freeaddrinfo(res); // free the linked list if no addresses were found
    return ""; // return empty string if no addresses were found
}

static std::string get_latest_version(const std::string &uid, std::string &err_msg)
{
    if (!network::have_network())
    {
        err_msg = "no network";
        return "";
    }
    std::string os_version = sys::os_version();
    std::string maixpy_version = sys::maixpy_version();
    try
    {
        httplib::Client cli("https://maixvision.sipeed.com");
        cli.enable_server_certificate_verification(false);
        // 添加请求头
        httplib::Headers headers;
        headers.insert({"token", "MaixVision2024"});
        const auto res = cli.Get("/api/v1/devices/encryption/version?uid=" + uid + "&os=" + os_version + "&maixpy=" + maixpy_version, headers);
        if(!res)
        {
            std::string ip = get_ip_by_hostname("maixvision.sipeed.com");
            if(ip.empty())
            {
                log::error("DNS resolve failed, please check network or DNS settings");
                err_msg = "DNS resolve failed, please check network or DNS settings";
                return "";
            }
            log::error("get latest version failed, http request failed");
            err_msg = "http request failed";
            return "";
        }
        const auto response = res.value();
        // 检查响应状态
        if (response.status == httplib::StatusCode::OK_200)
        {
            // 返回响应体
            return parse_version(response.body);
        }
        else
        {
            log::error(("get latest version failed" + std::to_string(response.status)).c_str());
            err_msg = "get latest version failed" + std::to_string(response.status);
            return "";
        }
    }
    catch (const std::exception &e)
    {
        // 处理异常
        log::error("get latest version exception");
        err_msg = "get latest version failed, " + std::string(e.what());
        return "";
    }
}

static bool need_upgrade(const std::string &latest, const std::string &curr)
{
    if (latest.empty())
        return false;
    if (curr.empty())
        return true;
    // Split version strings into parts
    std::vector<std::string> curr_parts = split(curr, '.');
    std::vector<std::string> latest_parts = split(latest, '.');

    // Compare each part of the version numbers
    for (size_t i = 0; i < curr_parts.size() && i < latest_parts.size(); ++i)
    {
        int curr_part = std::stoi(curr_parts[i]);
        int latest_part = std::stoi(latest_parts[i]);
        if (latest_part > curr_part)
        {
            return true;
        }
        else if (latest_part < curr_part)
        {
            return false;
        }
    }

    // If all parts are equal up to the shorter version's length, check for additional parts
    return latest_parts.size() > curr_parts.size();
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

static void update_battery_level(lv_obj_t *obj)
{
    lv_obj_t *bat = lv_obj_get_child(obj, 1);
    lv_obj_t *bat_icon = lv_obj_get_child(obj, 2);
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

void check_process(void *args)
{
    check_thread_exit = false;
    std::string key = sys::device_key();
    std::string err_msg = "";
    std::string latest_version = get_latest_version(key, err_msg);
    std::string curr_version = get_curr_version();
    while(!gui_destroyed)
    {
        log::info("check runtime upgrade");
        if(need_upgrade(latest_version, curr_version) && !gui_destroyed)
        {
            while(!lv_ui_mutex_lock(200))
            {
                time::sleep_ms(20);
            }
            lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
            lv_msgbox_add_title(mbox, _("Device Activation"));
            lv_msgbox_add_text(mbox, _("Connect to WiFi in \"settings\" to activate the device on first use."));
            lv_msgbox_add_close_button(mbox);
            lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, _("OK"));
            lv_obj_add_event_cb(btn, on_close_msg, LV_EVENT_CLICKED, mbox);
            lv_obj_add_event_cb(mbox, on_close_msg, LV_EVENT_DELETE, mbox);
            lv_obj_center(mbox);
            lv_ui_mutex_unlock();
        }
        break;
    }
    check_thread_exit = true;
}

void bar_update_process(void *args)
{
    bar_update_thread_exit = false;
    lv_obj_t *ui_bar = (lv_obj_t*) args;
    lv_obj_t *time = lv_obj_get_child(ui_bar, 0);

    while(!gui_destroyed)
    {
        if(!lv_ui_mutex_lock(200))
        {
            time::sleep_ms(1000);
            continue;
        }
        auto tm = time::localtime();
        lv_label_set_text_fmt(time, "%02d : %02d", tm->hour, tm->minute);
        update_battery_level(ui_bar);
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

    // check pmu and battery
    if(sys::device_id() == "maixcam_pro") {
        pmu = new ext_dev::pmu::PMU("axp2101");
        ext_dev::pmu::ChargerStatus charger_status = pmu->get_charger_status();
        if(pmu->is_bat_connect() && \
           charger_status != ext_dev::pmu::ChargerStatus::CHG_TRI_STATE && \
           charger_status != ext_dev::pmu::ChargerStatus::CHG_PRE_STATE) {
            log::info("PMU: Battery is connect.");
            int bar_height = 10;
            lv_obj_t *ui_bar = lv_obj_create(wrapper);
            lv_obj_clear_flag(ui_bar, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(ui_bar, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
            lv_obj_set_style_border_width(ui_bar, 0, LV_PART_MAIN);
            lv_obj_set_style_margin_all(ui_bar, 0, LV_PART_MAIN);
            lv_obj_set_size(ui_bar, lv_pct(100), lv_pct(bar_height));
            cont_pct_y = bar_height;
            cont_pct_height = 100 - bar_height;
            lv_obj_align(ui_bar, LV_ALIGN_TOP_MID, 0, 0);
            lv_obj_set_scrollbar_mode(ui_bar, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_radius(ui_bar, 0, 0);
            {
                lv_obj_t *label = lv_label_create(ui_bar);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
                auto time = time::localtime();
                lv_label_set_text_fmt(label, "%02d : %02d", time->hour, time->minute);
                lv_obj_center(label);
            }
            {
                lv_obj_t *label = lv_label_create(ui_bar);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
                lv_obj_set_width(label, lv_pct(10));
                lv_label_set_text(label, "0%");
                lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
                lv_obj_align(label, LV_ALIGN_LEFT_MID, lv_pct(83), 0);
            }
            {
                lv_obj_t *label = lv_label_create(ui_bar);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
                lv_label_set_text(label, LV_SYMBOL_BATTERY_EMPTY);
                lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
            }
            update_battery_level(ui_bar);
            bar_update_thread = new thread::Thread(bar_update_process, (void *)ui_bar);
            bar_update_thread->detach();
        } else {
            log::info("PMU: Battery not connect.");
            delete pmu;
            pmu = nullptr;
        }
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

    #define LIB_VERSION_FILE_PATH "/maixapp/maixcam_lib.version"

    // check lib file
    if(!fs::exists(LIB_VERSION_FILE_PATH))
    {
        log::info("runtime not installed");
        lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
        lv_msgbox_add_title(mbox, _("Device Activation"));
        lv_msgbox_add_text(mbox, _("Connect to WiFi in \"settings\" to activate the device on first use."));
        lv_msgbox_add_close_button(mbox);
        lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, _("OK"));
        lv_obj_add_event_cb(btn, on_close_msg, LV_EVENT_CLICKED, mbox);
        lv_obj_add_event_cb(mbox, on_close_msg, LV_EVENT_DELETE, mbox);
        lv_obj_center(mbox);
    }
    else
    {
        // 禁止检查运行库是否有更新
#if 0
        // check thread
        check_thread = new thread::Thread(check_process, nullptr);
        check_thread->detach();
#endif
    }
}


void launcher::home_items_free(Maix_GUI_Activity *activity, vector<app::APP_Info>& app_info, void* screen)
{
    gui_destroyed = true;

#if 0
    log::info("wait check thread exit");
    while(!check_thread_exit)
    {
        time::sleep_ms(20);
    }
    log::info("wait check thread exit done");
    delete check_thread;
    check_thread = nullptr;
#endif

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

