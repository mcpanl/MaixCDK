/**
 * 从 /maixapp/maixcam_lib.version 获取版本作为当前版本
 * 从 maixhub 获取文件最新版本
 * 对比是否需要更新，需要则提供一个按钮让用户点击下载
 */

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
#include <unistd.h>
#include "maix_network.hpp"
#include <iostream>
#include <cstring> // for memset
#include <netdb.h> // for getaddrinfo and struct addrinfo
#include <arpa/inet.h> // for inet_ntop

#include "httplib.h"

using namespace maix;

#define LIB_VERSION_FILE_PATH "/maixapp/maixcam_lib.version"

static maix::thread::Thread *load_info_thread = NULL, *upgrade_thread = NULL;
static lv_obj_t *label_remote = NULL;
static lv_obj_t *upgrade_btn = NULL, *label_btn = NULL, *label_curr = NULL;
static std::string curr_version = "";
static std::string latest_version = "";

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

static std::string update_sys_version_str(std::string &os_version)
{
    std::regex pattern(R"((\d{4}-\d{2}-\d{2}).*?(v\d+\.\d+\.\d+))");
    std::smatch match;
    if (std::regex_search(os_version, match, pattern)) {
        return match[1].str() + "-" + match[2].str();
    }
    return "unknown"; // 返回空字符串表示匹配失败
}


static void read_info_process(void *args)
{
    std::string key = sys::device_key();
    std::string err_msg = "";
    latest_version = get_latest_version(key, err_msg);
    lv_ui_mutex_lock(-1);
    if (latest_version.empty())
        lv_label_set_text(label_remote, (std::string(_("Latest")) + ": " + err_msg).c_str());
    else
        lv_label_set_text(label_remote, (std::string(_("Latest")) + ": " + latest_version).c_str());
    lv_ui_mutex_unlock();
    log::info("maixcam lib version: %s", latest_version.c_str());
    if (need_upgrade(latest_version, curr_version))
    {
        lv_ui_mutex_lock(-1);
        lv_label_set_text(label_remote, (std::string(_("Latest")) + ": " + latest_version).c_str());
        lv_obj_add_flag(upgrade_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_state(upgrade_btn, LV_STATE_DISABLED, false);
        lv_ui_mutex_unlock();
    }
    log::info("read_info_process exit");
}

static void upgrade_process(void *args)
{
    // download from server
    std::string key = sys::device_key();

    try
    {
        httplib::Client cli("https://maixvision.sipeed.com");
        cli.enable_server_certificate_verification(false);
        // 添加请求头
        httplib::Headers headers;
        headers.insert({"token", "MaixVision2024"});
        const auto res = cli.Get("/api/v1/devices/encryption?uid=" + key + "&version=" + latest_version, headers);
        if(!res)
        {
            log::error("get latest version failed");
            lv_ui_mutex_lock(-1);
            lv_label_set_text(label_remote, (std::string(_("Failed")) + ": " + std::string(_("network"))).c_str());
            lv_ui_mutex_unlock();
            return;
        }
        const auto response = res.value();
        // 检查响应状态
        if (response.status == httplib::StatusCode::OK_200)
        {
            // 写入 so 文件到  /usr/lib/libmaixcam_lib.so.{latest_version}
            std::string file_name = "/usr/lib/libmaixcam_lib.so." + latest_version;
            if (response.body.size() < 1024)
            {
                log::error("request lib failed: %s", response.body.c_str());
                lv_ui_mutex_lock(-1);
                lv_label_set_text(label_remote, _("Request lib failed"));
                lv_ui_mutex_unlock();
                return;
            }
            int count = 1;
            while(fs::exists(file_name))
            {
                file_name += "_" + std::to_string(count++);
            }
            log::info("open %s", file_name.c_str());
            fs::File *file = fs::open(file_name, "w");
            if (!file)
            {
                log::error("open write lib file failed");
                lv_ui_mutex_lock(-1);
                lv_label_set_text(label_remote, _("Write lib failed"));
                lv_ui_mutex_unlock();
                return;
            }
            log::info("write %s, len: %ld", file_name.c_str(), response.body.size());
            file->write(response.body.data(), response.body.size());
            file->close();
            delete file;
            log::info("write %s done", file_name.c_str());
            (void)!chmod(file_name.c_str(), 0775);
            err::Err e = fs::symlink(file_name, "/usr/lib/libmaixcam_lib.so", true);
            if (e != err::Err::ERR_NONE)
            {
                log::error("create symlink failed: %s", err::to_str(e).c_str());
                lv_ui_mutex_lock(-1);
                lv_label_set_text(label_remote, (std::string(_("symlink lib failed")) + ": " + err::to_str(e)).c_str());
                lv_ui_mutex_unlock();
                return;
            }
            log::info("update symlink done");
        }
        else
        {
            log::error(("get latest version failed" + std::to_string(response.status)).c_str());
            lv_ui_mutex_lock(-1);
            lv_label_set_text(label_remote, (std::string(_("Failed, code")) + ": " + std::to_string(response.status)).c_str());
            lv_ui_mutex_unlock();
            return;
        }
    }
    catch (const std::exception &e)
    {
        // 处理异常
        log::error("get latest version exception");
        lv_ui_mutex_lock(-1);
        lv_label_set_text(label_remote, (std::string(_("Exception")) + " " + std::string(e.what())).c_str());
        lv_ui_mutex_unlock();
        return;
    }

    fs::File *file = fs::open(LIB_VERSION_FILE_PATH, "w");
    if (!file)
    {
        log::error("open write " LIB_VERSION_FILE_PATH " failed");
        lv_ui_mutex_lock(-1);
        lv_label_set_text(label_remote, _("Write file failed"));
        lv_ui_mutex_unlock();
        return;
    }
    else
    {
        file->write(latest_version.c_str(), latest_version.size());
        file->close();
        delete file;
    }
    sync();
    curr_version = latest_version;
    lv_ui_mutex_lock(-1);
    lv_label_set_text(label_remote, (std::string(_("Latest")) + ": " + latest_version).c_str());
    lv_label_set_text(label_curr, (std::string(_("Current")) + ": " + curr_version).c_str());
    lv_label_set_text(label_btn, _("Upgrade"));
    lv_ui_mutex_unlock();
}

std::string get_curr_version()
{
    fs::File *file = fs::open(LIB_VERSION_FILE_PATH, "r");
    if (!file)
    {
        return "";
    }
    else
    {
        std::string *version = file->readline();
        std::string curr_version = *version;
        delete version;
        file->close();
        delete file;
        return curr_version;
    }
}

void on_upgrade_lib_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
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

    lv_obj_t *container1 = lv_obj_create(container);
    lv_obj_set_size(container1, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(container1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * label_sys_version = lv_label_create(container1);
    std::string sys_version = maix::sys::os_version();
    lv_label_set_text(label_sys_version, (std::string(_("System Version")) + ": " + update_sys_version_str(sys_version)).c_str());

    // lv_obj_t * label_maixpy_version = lv_label_create(container1);
    // std::string maixpy_version = maix::sys::maixpy_version();
    // lv_label_set_text(label_maixpy_version, ("MaixPy " + std::string(_("Version")) + ": " + maixpy_version).c_str());

    lv_obj_t *container2 = lv_obj_create(container);
    lv_obj_set_size(container2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(container2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * label_runtime = lv_label_create(container2);
    lv_label_set_text(label_runtime, _("Runtime Version"));

    label_curr = lv_label_create(container2);
    std::string curr = get_curr_version();
    if (curr.empty())
    {
        lv_label_set_text(label_curr, _("Not install yet"));
    }
    else
    {
        lv_label_set_text(label_curr, (std::string(_("Current")) + ": " + curr).c_str());
        curr_version = curr;
    }

    label_remote = lv_label_create(container2);
    lv_label_set_text(label_remote, (std::string(_("Latest")) + ": " + _("loading") + " ...").c_str());

    upgrade_btn = lv_btn_create(container2);
    label_btn = lv_label_create(upgrade_btn);
    lv_label_set_text(label_btn, _("Upgrade"));
    lv_obj_remove_flag(upgrade_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_state(upgrade_btn, LV_STATE_DISABLED, true);

    lv_obj_add_event_cb(
        upgrade_btn, [](lv_event_t *e)
        {
        if (need_upgrade(latest_version, curr_version))
        {
            lv_obj_remove_flag(upgrade_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_state(upgrade_btn, LV_STATE_DISABLED, true);
            log::info("upgrade now");
            lv_label_set_text(label_btn, _("Upgrading") );
            if(upgrade_thread)
            {
                upgrade_thread->join();
                delete upgrade_thread;
                upgrade_thread = NULL;
            }
            upgrade_thread = new maix::thread::Thread(upgrade_process, NULL);
            upgrade_thread->detach();
        } },
        LV_EVENT_CLICKED, NULL);

    if (load_info_thread)
    {
        load_info_thread->join();
        delete load_info_thread;
        load_info_thread = NULL;
    }
    load_info_thread = new maix::thread::Thread(read_info_process, NULL);
    load_info_thread->detach();
}

void on_upgrade_lib_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    if (upgrade_thread)
    {
        upgrade_thread->join();
        delete upgrade_thread;
        upgrade_thread = NULL;
    }
    if (load_info_thread)
    {
        load_info_thread->join();
        delete load_info_thread;
        load_info_thread = NULL;
    }
}

static int upgrade_process_pure()
{
    // download from server
    std::string key = sys::device_key();
    std::string err_msg = "";
    std::string curr_version = get_curr_version();
    log::info("Current runtime version: %s", curr_version.c_str());
    std::string latest_version = get_latest_version(key, err_msg);
    if (latest_version.empty())
    {
        log::error("Install runtime error: %s", err_msg.c_str());
        return -1;
    }
    log::info("Latest lib version: %s", latest_version.c_str());
    if (!need_upgrade(latest_version, curr_version))
    {
        log::info("Already latest, skip upgrade");
        return 0;
    }

    try
    {
        httplib::Client cli("https://maixvision.sipeed.com");
        cli.enable_server_certificate_verification(false);
        // 添加请求头
        httplib::Headers headers;
        headers.insert({"token", "MaixVision2024"});
        const auto res = cli.Get("/api/v1/devices/encryption?uid=" + key + "&version=" + latest_version, headers);
        if(!res)
        {
            log::error("network request failed");
            return -8;
        }
        const auto response = res.value();
        // 检查响应状态
        if (response.status == httplib::StatusCode::OK_200)
        {
            // 写入 so 文件到  /usr/lib/libmaixcam_lib.so.{latest_version}
            std::string file_name = "/usr/lib/libmaixcam_lib.so." + latest_version;
            if (response.body.size() < 1024)
            {
                log::error("request lib failed: %s", response.body.c_str());
                return -2;
            }
            int count = 1;
            while(fs::exists(file_name))
            {
                file_name += "_" + std::to_string(count++);
            }
            log::info("open %s", file_name.c_str());
            fs::File *file = fs::open(file_name, "w");
            if (!file)
            {
                log::error("open write lib file failed");
                return -3;
            }
            log::info("write %s, len: %ld", file_name.c_str(), response.body.size());
            file->write(response.body.data(), response.body.size());
            file->close();
            delete file;
            log::info("write %s done", file_name.c_str());
            (void)!chmod(file_name.c_str(), 0775);
            err::Err e = fs::symlink(file_name, "/usr/lib/libmaixcam_lib.so", true);
            if (e != err::Err::ERR_NONE)
            {
                log::error("create symlink failed: %s", err::to_str(e).c_str());
                return -4;
            }
            log::info("update symlink done");
        }
        else
        {
            log::error(("get latest version failed" + std::to_string(response.status)).c_str());
            return -5;
        }
    }
    catch (const std::exception &e)
    {
        // 处理异常
        log::error("get latest version exception");
        return -6;
    }

    fs::File *file = fs::open(LIB_VERSION_FILE_PATH, "w");
    if (!file)
    {
        log::error("open write " LIB_VERSION_FILE_PATH " failed");
        return -7;
    }
    else
    {
        file->write(latest_version.c_str(), latest_version.size());
        file->close();
        delete file;
    }
    sync();
    log::info("update runtime to %s", latest_version.c_str());
    return 0;
}

int install_runtime()
{
    return upgrade_process_pure();
}

