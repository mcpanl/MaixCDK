#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>
#include <regex>

#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const unsigned long long max_mem = 256 * 1024 * 1024;
static maix::thread::Thread *load_info_thread = NULL;
static bool should_exit = false;
static unsigned long long total_memory = 0;
static unsigned long long used_memory = 0;
static unsigned long long total_space = 0;
static unsigned long long used_space = 0;
static std::string device_name = "MaixCAM";
static std::string camera_name = "";
static std::string host_domain = "maixcam-xxxx.local";
static std::vector<std::string> ip_addresses;
static std::string sys_version = "0.0.0";
static std::string maixpy_version = "0.0.0";
static unsigned long freq_cpu = 0;
static unsigned long freq_npu = 0;
static float temp_cpu = 0;
static lv_obj_t *label_device_name = NULL;
static lv_obj_t *label_camera_name = NULL;
static lv_obj_t *content_device_name = NULL;
static lv_obj_t *content_camera_name = NULL;
static lv_obj_t *label_host_domain = NULL;
static lv_obj_t *content_host_domain = NULL;
static lv_obj_t *label_ip_address = NULL;
static lv_obj_t *layout_ip_address = NULL;
static lv_obj_t *content_ip_address = NULL;
static lv_obj_t *label_sys_version = NULL;
static lv_obj_t *content_sys_version = NULL;
static lv_obj_t *label_maixpy_version = NULL;
static lv_obj_t *content_maixpy_version = NULL;
static lv_obj_t *label_device_key = NULL;
static lv_obj_t *content_device_key = NULL;
static lv_obj_t *label_memory = NULL;
static lv_obj_t *content_memory = NULL;
static lv_obj_t *label_space = NULL;
static lv_obj_t *content_space = NULL;
static lv_obj_t *label_temp_cpu = NULL;
static lv_obj_t *content_temp_cpu = NULL;
static lv_obj_t *label_freq_cpu = NULL;
static lv_obj_t *content_freq_cpu = NULL;
static lv_obj_t *label_freq_npu = NULL;
static lv_obj_t *content_freq_npu = NULL;

static std::string get_device_name()
{
    return "AI Camera2.0";
}

static std::string get_host_name()
{
    return maix::sys::host_name();
}

static std::string get_sys_version()
{
    return maix::sys::os_version();
}

static std::string get_device_key()
{
    return maix::sys::device_key();
}

static void get_system_memory_usage(unsigned long long *used, unsigned long long *total) {
    FILE* file = fopen("/proc/meminfo", "r");
    if (!file) {
        perror("Cannot open /proc/meminfo");
        return;
    }

    unsigned long total_memory = 0;
    unsigned long free_memory = 0;
    char line[256];

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "MemTotal: %lu kB", &total_memory) == 1) {
            // printf("Total memory: %lu kB\n", total_memory);
        }
        if (sscanf(line, "MemAvailable: %lu kB", &free_memory) == 1) {
            // printf("MemAvailable memory: %lu kB\n", free_memory);
            break;
        }
    }

    fclose(file);

    *used = (total_memory - free_memory) * 1024;
    *total = total_memory * 1024;
}

static void get_space_usage(unsigned long long *used, unsigned long long *total) {
    auto usage = maix::sys::disk_usage("/");
    if(usage.size() <= 0)
    {
        *total = 0;
        *used = 0;
        return;
    }
    *total = usage["total"];
    *used = usage["used"];
}

static void get_freq(unsigned long *cpu_freq, unsigned long *npu_freq)
{
    *cpu_freq = 0;
    *npu_freq = 0;
    std::map<std::string, unsigned long> cpu = maix::sys::cpu_freq();
    for(auto c : cpu)
    {
        *cpu_freq = c.second;
        break;
    }
    std::map<std::string, unsigned long> npu = maix::sys::npu_freq();
    for(auto c : npu)
    {
        *npu_freq = c.second;
        break;
    }
}

static float get_cpu_temp()
{
    for(auto i: maix::sys::cpu_temp())
    {
        return i.second;
    }
    return 0;
}

static std::vector<std::string> get_ip_addresses()
{
    std::map<std::string, std::string> ips = maix::sys::ip_address();
    std::vector<std::string> res;
    for(auto ip : ips)
    {
        res.push_back(ip.first + ": " + ip.second);
    }
    return res;
}

static std::string connect_ips(std::vector<std::string> &ips, const std::string &split_str)
{
    std::string result;
    for (size_t i = 0; i < ips.size(); i++)
    {
        if (i > 0)
            result += split_str;
        result += ips[i];
    }
    return result;
}

static std::string byte_to_human_readable(unsigned long long bytes)
{
    return maix::sys::bytes_to_human(bytes);
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
    if(should_exit)
        return;

    // memory
    get_system_memory_usage(&used_memory, &total_memory);
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text_fmt(content_memory, "%s/%s\n%s Kernel used", byte_to_human_readable(used_memory).c_str(), byte_to_human_readable(total_memory).c_str(), byte_to_human_readable(max_mem - total_memory).c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // space
    get_space_usage(&used_space, &total_space);
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text_fmt(content_space, "%s/%s", byte_to_human_readable(used_space).c_str(), byte_to_human_readable(total_space).c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // cpu npu freq
    get_freq(&freq_cpu, &freq_npu);
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text_fmt(content_freq_cpu, "%lu MHz", freq_cpu/1000000);
    lv_label_set_text_fmt(content_freq_npu, "%lu MHz", freq_npu/1000000);
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // get device name
    device_name = get_device_name();
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text(content_device_name, device_name.c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // get host name
    host_domain = get_host_name();
    if(!host_domain.empty())
    {
        host_domain += ".local";
    }
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text(content_host_domain, host_domain.c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // get sys version
    sys_version = get_sys_version();
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text(content_sys_version, update_sys_version_str(sys_version).c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // get ip address
    ip_addresses = get_ip_addresses();
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_obj_set_size(layout_ip_address, lv_pct(100), ip_addresses.size() > 0 ? 30 * ip_addresses.size() + 20: 60);
    lv_label_set_text(content_ip_address, connect_ips(ip_addresses, "\n").c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // get device key
    std::string device_key = get_device_key();
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text(content_device_key, device_key.c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // // get maixpy version
    // maixpy_version = sys::maixpy_version();
    // lv_ui_mutex_lock(-1);
    // if(should_exit) {lv_ui_mutex_unlock(); return;}
    // lv_label_set_text(content_maixpy_version, maixpy_version.c_str());
    // lv_ui_mutex_unlock();

    // get device name
    camera_name = camera::get_device_name();
    lv_ui_mutex_lock(-1);
    if(should_exit) {lv_ui_mutex_unlock(); return;}
    lv_label_set_text(content_camera_name, camera_name.c_str());
    lv_ui_mutex_unlock();
    if(should_exit) return;

    // get cpu temp
    while(!should_exit)
    {
        temp_cpu = get_cpu_temp();
        lv_ui_mutex_lock(-1);
        if(should_exit) {lv_ui_mutex_unlock(); return;}
        lv_label_set_text_fmt(content_temp_cpu, (std::string("%.2f") + _("℃")).c_str(), temp_cpu);
        lv_ui_mutex_unlock();
        maix::time::sleep(1);
    }

}

void on_iofo_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    std::string split_str = ": ";
    std::string locale = i18n::get_locale();

    if(load_info_thread)
    {
        load_info_thread->join();
        delete load_info_thread;
    }
    should_exit = false;
    load_info_thread = new maix::thread::Thread(read_info_process, NULL);
    load_info_thread->detach();

    lv_obj_t *root = (lv_obj_t *)obj;
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(container, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(container, theme_text_color, LV_PART_MAIN);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(container);
    lv_label_set_text(label, _("Device Info"));

    // all items flex row layout， 2 columns， first column is label(50% width of layout)， second column is content
    // first label right align， second content left align

    // device name
    lv_obj_t *layout_device_name = lv_obj_create(container);
    lv_obj_set_size(layout_device_name, lv_pct(100), 60);
    lv_obj_set_layout(layout_device_name, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_device_name, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_device_name, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(layout_device_name, 0, LV_PART_MAIN);
    lv_obj_clear_flag(layout_device_name, LV_OBJ_FLAG_SCROLLABLE);
    label_device_name = lv_label_create(layout_device_name);
    lv_label_set_text(label_device_name, (_("Device Name") + split_str).c_str());
    lv_obj_set_width(label_device_name, lv_pct(35));
    lv_obj_set_style_text_align(label_device_name, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_device_name = lv_label_create(layout_device_name);
    lv_label_set_text(content_device_name, device_name.c_str());
    lv_obj_set_style_text_align(content_device_name, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // host domain
    lv_obj_t *layout_host_domain = lv_obj_create(container);
    lv_obj_set_size(layout_host_domain, lv_pct(100), 60);
    lv_obj_set_layout(layout_host_domain, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_host_domain, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_host_domain, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_host_domain, LV_OBJ_FLAG_SCROLLABLE);
    label_host_domain = lv_label_create(layout_host_domain);
    lv_label_set_text(label_host_domain, (_("Host Domain") + split_str).c_str());
    lv_obj_set_width(label_host_domain, lv_pct(35));
    lv_obj_set_style_text_align(label_host_domain, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_host_domain = lv_label_create(layout_host_domain);
    lv_label_set_text(content_host_domain, host_domain.c_str());
    lv_obj_set_style_text_align(content_host_domain, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // ip address
    layout_ip_address = lv_obj_create(container);
    lv_obj_set_size(layout_ip_address, lv_pct(100), 60);
    lv_obj_set_layout(layout_ip_address, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_ip_address, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_ip_address, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_ip_address, LV_OBJ_FLAG_SCROLLABLE);
    label_ip_address = lv_label_create(layout_ip_address);
    lv_label_set_text(label_ip_address, (std::string("IP ") + _("Address") + split_str).c_str());
    lv_obj_set_width(label_ip_address, lv_pct(35));
    lv_obj_set_style_text_align(label_ip_address, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_ip_address = lv_label_create(layout_ip_address);
    lv_label_set_text(content_ip_address, connect_ips(ip_addresses, "\n").c_str());
    lv_obj_set_style_text_align(content_ip_address, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // sys version
    lv_obj_t *layout_sys_version = lv_obj_create(container);
    lv_obj_set_size(layout_sys_version, lv_pct(100), 60);
    lv_obj_set_layout(layout_sys_version, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_sys_version, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_sys_version, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_sys_version, LV_OBJ_FLAG_SCROLLABLE);
    label_sys_version = lv_label_create(layout_sys_version);
    lv_label_set_text(label_sys_version, (_("System Version") + split_str).c_str());
    lv_obj_set_width(label_sys_version, lv_pct(35));
    lv_obj_set_style_text_align(label_sys_version, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_sys_version = lv_label_create(layout_sys_version);
    lv_label_set_text(content_sys_version, sys_version.c_str());
    lv_obj_set_style_text_align(content_sys_version, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // // maixpy version
    // lv_obj_t *layout_maixpy_version = lv_obj_create(container);
    // lv_obj_set_size(layout_maixpy_version, lv_pct(100), 60);
    // lv_obj_set_layout(layout_maixpy_version, LV_LAYOUT_FLEX);
    // lv_obj_set_flex_flow(layout_maixpy_version, LV_FLEX_FLOW_ROW);
    // lv_obj_set_flex_align(layout_maixpy_version, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // lv_obj_clear_flag(layout_maixpy_version, LV_OBJ_FLAG_SCROLLABLE);
    // label_maixpy_version = lv_label_create(layout_maixpy_version);
    // lv_label_set_text(label_maixpy_version, (std::string("MaixPy ") + _("Version") + split_str).c_str());
    // lv_obj_set_width(label_maixpy_version, lv_pct(35));
    // lv_obj_set_style_text_align(label_maixpy_version, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    // content_maixpy_version = lv_label_create(layout_maixpy_version);
    // lv_label_set_text(content_maixpy_version, maixpy_version.c_str());
    // lv_obj_set_style_text_align(content_maixpy_version, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // device key
    lv_obj_t *layout_device_key = lv_obj_create(container);
    lv_obj_set_size(layout_device_key, lv_pct(100), 60);
    lv_obj_set_layout(layout_device_key, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_device_key, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_device_key, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_device_key, LV_OBJ_FLAG_SCROLLABLE);
    label_device_key = lv_label_create(layout_device_key);
    lv_label_set_text(label_device_key, (_("Device Key") + split_str).c_str());
    lv_obj_set_width(label_device_key, lv_pct(35));
    lv_obj_set_style_text_align(label_device_key, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_device_key = lv_label_create(layout_device_key);
    lv_label_set_text(content_device_key, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    lv_obj_set_style_text_align(content_device_key, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // camera device name
    lv_obj_t *layout_camera_name = lv_obj_create(container);
    lv_obj_set_size(layout_camera_name, lv_pct(100), 60);
    lv_obj_set_layout(layout_camera_name, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_camera_name, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_camera_name, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(layout_camera_name, 0, LV_PART_MAIN);
    lv_obj_clear_flag(layout_camera_name, LV_OBJ_FLAG_SCROLLABLE);
    label_camera_name = lv_label_create(layout_camera_name);
    lv_label_set_text(label_camera_name, (_("Camera Name") + split_str).c_str());
    lv_obj_set_width(label_camera_name, lv_pct(35));
    lv_obj_set_style_text_align(label_camera_name, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_camera_name = lv_label_create(layout_camera_name);
    lv_label_set_text(content_camera_name, camera_name.c_str());
    lv_obj_set_style_text_align(content_camera_name, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // memory
    lv_obj_t *layout_memory = lv_obj_create(container);
    lv_obj_set_size(layout_memory, lv_pct(100), 80);
    lv_obj_set_layout(layout_memory, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_memory, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_memory, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_memory, LV_OBJ_FLAG_SCROLLABLE);
    label_memory = lv_label_create(layout_memory);
    lv_label_set_text(label_memory, (_("Memory") + split_str).c_str());
    lv_obj_set_width(label_memory, lv_pct(35));
    lv_obj_set_style_text_align(label_memory, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_memory = lv_label_create(layout_memory);
    lv_label_set_text_fmt(content_memory, "%s/%s\n%s Kernel used", byte_to_human_readable(used_memory).c_str(), byte_to_human_readable(total_memory).c_str(), byte_to_human_readable(max_mem - total_memory).c_str());
    lv_obj_set_style_text_align(content_memory, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // space
    lv_obj_t *layout_space = lv_obj_create(container);
    lv_obj_set_size(layout_space, lv_pct(100), 60);
    lv_obj_set_layout(layout_space, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_space, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_space, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_space, LV_OBJ_FLAG_SCROLLABLE);
    label_space = lv_label_create(layout_space);
    lv_label_set_text(label_space, (_("Space") + split_str).c_str());
    lv_obj_set_width(label_space, lv_pct(35));
    lv_obj_set_style_text_align(label_space, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_space = lv_label_create(layout_space);
    lv_label_set_text_fmt(content_space, "%s/%s", byte_to_human_readable(used_space).c_str(), byte_to_human_readable(total_space).c_str());
    lv_obj_set_style_text_align(content_space, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // temp cpu
    lv_obj_t *layout_temp_cpu = lv_obj_create(container);
    lv_obj_set_size(layout_temp_cpu, lv_pct(100), 60);
    lv_obj_set_layout(layout_temp_cpu, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_temp_cpu, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_temp_cpu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_temp_cpu, LV_OBJ_FLAG_SCROLLABLE);
    label_temp_cpu = lv_label_create(layout_temp_cpu);
    lv_label_set_text(label_temp_cpu, (std::string("CPU ") + _("Temp") + split_str).c_str());
    lv_obj_set_width(label_temp_cpu, lv_pct(35));
    lv_obj_set_style_text_align(label_temp_cpu, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_temp_cpu = lv_label_create(layout_temp_cpu);
    lv_label_set_text_fmt(content_temp_cpu, (std::string("%.2f") + _("℃")).c_str(), temp_cpu);
    lv_obj_set_style_text_align(content_temp_cpu, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // freq cpu
    lv_obj_t *layout_freq_cpu = lv_obj_create(container);
    lv_obj_set_size(layout_freq_cpu, lv_pct(100), 60);
    lv_obj_set_layout(layout_freq_cpu, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_freq_cpu, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_freq_cpu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_freq_cpu, LV_OBJ_FLAG_SCROLLABLE);
    label_freq_cpu = lv_label_create(layout_freq_cpu);
    lv_label_set_text(label_freq_cpu, (std::string("CPU ") + _("Freq") + split_str).c_str());
    lv_obj_set_width(label_freq_cpu, lv_pct(35));
    lv_obj_set_style_text_align(label_freq_cpu, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_freq_cpu = lv_label_create(layout_freq_cpu);
    lv_label_set_text_fmt(content_freq_cpu, "%lu MHz", freq_cpu/1000);
    lv_obj_set_style_text_align(content_freq_cpu, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    // freq npu
    lv_obj_t *layout_freq_npu = lv_obj_create(container);
    lv_obj_set_size(layout_freq_npu, lv_pct(100), 60);
    lv_obj_set_layout(layout_freq_npu, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(layout_freq_npu, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(layout_freq_npu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(layout_freq_npu, LV_OBJ_FLAG_SCROLLABLE);
    label_freq_npu = lv_label_create(layout_freq_npu);
    lv_label_set_text(label_freq_npu, (std::string("NPU ") + _("Freq") + split_str).c_str());
    lv_obj_set_width(label_freq_npu, lv_pct(35));
    lv_obj_set_style_text_align(label_freq_npu, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    content_freq_npu = lv_label_create(layout_freq_npu);
    lv_label_set_text_fmt(content_freq_npu, "%lu MHz", freq_npu/1000);
    lv_obj_set_style_text_align(content_freq_npu, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
}

void on_iofo_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    should_exit = true;
}
