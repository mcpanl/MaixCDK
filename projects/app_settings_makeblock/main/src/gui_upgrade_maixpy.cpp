#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_gui.hpp"
#include "maix_basic.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>
#include <sys/wait.h>

static lv_obj_t *lates_maixpy_info = NULL;
static lv_obj_t *label_version = NULL;
static lv_obj_t *btn_upgrade = NULL;
static lv_obj_t *upgrading_msg = NULL;
static lv_obj_t *ddlist = NULL;
static lv_obj_t *label_upgrade = NULL;
static maix::thread::Thread *load_info_thread = NULL;
static maix::thread::Thread *load_info_thread2 = NULL;
static maix::thread::Thread *upgrade_thread = NULL;
static std::string maixpy_version = "";
static std::string latest_version = "";
static bool process_running = false, process_running2 = false;
static bool process_should_exit = false, process_should_exit2 = false;
static bool upgrading = false;
static int mirror_idx = 0;
static std::vector<std::string> mirrors = {"pypi", "aliyun", "ustc", "163", "douban", "tsinghua"};
static std::vector<std::string> mirrors_url = {"https://pypi.org/simple", "https://mirrors.aliyun.com/pypi/simple", "https://pypi.mirrors.ustc.edu.cn/simple", "https://mirrors.163.com/pypi/simple", "https://pypi.douban.com/simple", "https://pypi.tuna.tsinghua.edu.cn/simple"};

static std::string get_maixpy_version()
{
    // std::string version = "0.0.0";
    // FILE *fp = popen("python3 -c \"import maix;print(maix.__version__)\"", "r");
    // if (fp == NULL)
    // {
    //     return version;
    // }
    // char buf[32] = {0};
    // // get lines, find line starts with number
    // while (fgets(buf, sizeof(buf), fp) != NULL)
    // {
    //     if (buf[0] >= '0' && buf[0] <= '9')
    //     {
    //         version = buf;
    //         // remove \n
    //         version.erase(version.find_last_not_of("\n") + 1);
    //         break;
    //     }
    // }
    // pclose(fp);
    // return version;
    return sys::maixpy_version();
}

static std::string get_latest_maixpy_version(bool *cancel)
{
    std::string version = "";
    int pipe_fd[2];

    // Create a pipe for communication
    if (pipe(pipe_fd) == -1)
    {
        perror("pipe");
        return version;
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        // Fork failed
        perror("fork");
        return version;
    }

    if (pid == 0)
    {
        // Child process
        close(pipe_fd[0]); // Close the read end
        dup2(pipe_fd[1], STDOUT_FILENO); // Redirect stdout to the write end of the pipe
        close(pipe_fd[1]); // Close the original write end

        // Execute the Python command
        execlp("python3", "python3", "-c",
               "import requests; print(requests.get('https://pypi.org/pypi/maixpy/json').json()['info']['version'])",
               (char *)NULL);
        // If execlp fails, exit
        perror("execlp");
        exit(EXIT_FAILURE);
    }
    else
    {
        // Parent process
        close(pipe_fd[1]); // Close the write end

        // Set the file descriptor to non-blocking mode
        int flags = fcntl(pipe_fd[0], F_GETFL, 0);
        fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

        char buf[32] = {0};
        ssize_t n;

        // Periodically check for cancellation and read from the pipe
        while (!*cancel)
        {
            while ((n = read(pipe_fd[0], buf, sizeof(buf) - 1)) > 0)
            {
                buf[n] = '\0'; // Null-terminate the buffer
                if (buf[0] >= '0' && buf[0] <= '9')
                {
                    version = buf;
                    // Remove \n
                    version.erase(version.find_last_not_of("\n") + 1);
                    break;
                }
            }

            if (n == 0)
            {
                // End of output
                break;
            }
            else if (n < 0 && errno != EAGAIN)
            {
                // Error other than EAGAIN
                perror("read");
                break;
            }

            usleep(100000); // 100 ms
        }

        if (*cancel)
        {
            // Cancel the child process
            kill(pid, SIGTERM);
        }

        // Wait for the child process to terminate
        int status;
        waitpid(pid, &status, 0);

        close(pipe_fd[0]); // Close the read end
    }

    return version;
}

static bool need_upgrade(std::string old, std::string latest)
{
    if (old.empty() || latest.empty())
    {
        return false;
    }
    if (old == latest)
    {
        return false;
    }
    std::vector<std::string> old_v;
    std::vector<std::string> latest_v;
    std::string tmp;
    for (size_t i = 0; i < old.size(); i++)
    {
        if (old[i] == '.')
        {
            old_v.push_back(tmp);
            tmp.clear();
        }
        else
        {
            tmp += old[i];
        }
    }
    old_v.push_back(tmp);
    tmp.clear();
    for (size_t i = 0; i < latest.size(); i++)
    {
        if (latest[i] == '.')
        {
            latest_v.push_back(tmp);
            tmp.clear();
        }
        else
        {
            tmp += latest[i];
        }
    }
    latest_v.push_back(tmp);
    tmp.clear();
    if (old_v.size() != latest_v.size())
    {
        return true;
    }
    for (size_t i = 0; i < old_v.size(); i++)
    {
        if (std::stoi(old_v[i]) > std::stoi(latest_v[i]))
            return false;
        if (std::stoi(old_v[i]) < std::stoi(latest_v[i]))
        {
            return true;
        }
    }
    return false;
}

static void read_info_process(void *args)
{
    log::info("get maixpy version");
    maixpy_version = get_maixpy_version();
    log::info("get maixpy version done: %s", maixpy_version.c_str());
    if(process_should_exit)
    {
        process_running =false;
        return;
    }
    lv_ui_mutex_lock(-1);
    lv_label_set_text(label_version, (std::string("MaixPy ") + _("version") + std::string(": v") + maixpy_version).c_str());
    lv_ui_mutex_unlock();
    log::info("MaixPy version: %s", maixpy_version.c_str());
    process_running = false;
}

static void read_info_process2(void *args)
{
    log::info("get maixpy latest version");
    std::string latest_version = get_latest_maixpy_version(&process_should_exit2);
    log::info("Latest version: %s", latest_version.c_str());
    if(process_should_exit2)
    {
        process_running2 =false;
        return;
    }
    if(latest_version.empty())
    {
        lv_ui_mutex_lock(-1);
        lv_label_set_text(lates_maixpy_info, _("Failed to get latest version"));
        lv_ui_mutex_unlock();
        process_running2 = false;
        return;
    }
    lv_ui_mutex_lock(-1);
    lv_label_set_text(lates_maixpy_info, (std::string(_("Latest version")) + ": v" + latest_version).c_str());
    lv_ui_mutex_unlock();
    // wait process 1 exit
    while (process_running)
    {
        maix::thread::sleep_ms(100);
    }
    if(need_upgrade(maixpy_version, latest_version))
    {
        lv_ui_mutex_lock(-1);
        lv_label_set_text(label_upgrade, _("Upgrade"));
        lv_obj_set_state(btn_upgrade, LV_STATE_DISABLED, false);
        lv_ui_mutex_unlock();
    }
    else
    {
        lv_ui_mutex_lock(-1);
        lv_label_set_text(label_upgrade, _("Already latest"));
        lv_obj_set_state(btn_upgrade, LV_STATE_DISABLED, true);
        lv_ui_mutex_unlock();
    }
    process_running2 = false;
}

static void upgrade_process(void *args)
{
    // execute pip install -U maixpy -i mirror_url[mirror_idx]
    // and watch stdout, stderr, show on screen in real time
    std::string cmd = "pip install -U maixpy -i " + mirrors_url[mirror_idx];
    log::info("upgrade cmd: %s", cmd.c_str());
    FILE *fp = popen(cmd.c_str(), "r");
    if (fp == NULL)
    {
        log::error("upgrade failed");
        return;
    }
    char buf[1024] = {0};
    lv_ui_mutex_lock(-1);
    std::string msg = lv_label_get_text(upgrading_msg);
    lv_ui_mutex_unlock();
    while (fgets(buf, sizeof(buf), fp) != NULL)
    {
        log::info("upgrade: %s", buf);
        msg += buf;
        lv_ui_mutex_lock(-1);
        lv_label_set_text(upgrading_msg, msg.c_str());
        lv_ui_mutex_unlock();
    }
    pclose(fp);
    lv_ui_mutex_lock(-1);
    lv_label_set_text(label_upgrade, _("Loading ..."));
    lv_ui_mutex_unlock();
    process_running = true;
    process_running2 = true;
    if(load_info_thread)
    {
        load_info_thread->join();
        delete load_info_thread;
        load_info_thread = NULL;
    }
    if(load_info_thread2)
    {
        load_info_thread2->join();
        delete load_info_thread2;
        load_info_thread2 = NULL;
    }
    load_info_thread = new maix::thread::Thread(read_info_process, NULL);
    load_info_thread2 = new maix::thread::Thread(read_info_process2, NULL);
    load_info_thread->detach();
    load_info_thread2->detach();
    upgrading = false;
}

static void on_btn_upgrade(lv_event_t *e)
{
    // if(upgrading)
    //     return;
    // upgrading = true;
    // lv_label_set_text(upgrading_msg, (std::string(_("Upgrading from")) + " " + mirrors[mirror_idx] + "\n").c_str());
    // if(upgrade_thread)
    // {
    //     upgrade_thread->join();
    //     delete upgrade_thread;
    //     upgrade_thread = NULL;
    // }
    // lv_label_set_text(label_upgrade, _("Upgrading ..."));
    // lv_obj_set_state(btn_upgrade, LV_STATE_DISABLED, true);
    // upgrade_thread = new maix::thread::Thread(upgrade_process, NULL);
    // upgrade_thread->detach();

    lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
    lv_msgbox_add_title(mbox, _("Warning"));
    lv_msgbox_add_text(mbox, _("Please visit documentation"));
    lv_msgbox_add_close_button(mbox);
}

void on_upgrade_maixpy_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    std::string locale = i18n::get_locale();

    process_running = true;
    process_running2 = true;
    process_should_exit = false;
    process_should_exit2 = false;

    load_info_thread = new maix::thread::Thread(read_info_process, NULL);
    load_info_thread2 = new maix::thread::Thread(read_info_process2, NULL);
    load_info_thread->detach();
    load_info_thread2->detach();

    lv_obj_t *root = (lv_obj_t *)obj;
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);
    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(container, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(container, theme_text_color, LV_PART_MAIN);
    // flex col layout, first is MaixPy version: v4.0.0
    // second is checking latest version loading icon
    // third is upgrade to latest version button
    // get MaixPy version and latest version should put in a new thread
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // System version
    lv_obj_t * tmp_label = lv_label_create(container); // padding
    lv_label_set_text(tmp_label, "");
    lv_obj_t * label_sys_version = lv_label_create(container);
    std::string sys_version = maix::sys::os_version();
    lv_label_set_text(label_sys_version, (std::string(_("System Version")) + ": " + sys_version).c_str());

    // MaixPy version
    label_version = lv_label_create(container);
    lv_label_set_text(label_version, (std::string("MaixPy ") + _("version") + ": " + std::string(_("loading")) + " ...").c_str());

    lates_maixpy_info = lv_label_create(container);
    lv_label_set_text(lates_maixpy_info, (std::string(_("Latest version")) + ": " + std::string(_("loading")) + " ...").c_str());


    lv_obj_t *container2 = lv_obj_create(container);
    lv_obj_set_style_radius(container2, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_top(container2, 20, LV_PART_MAIN);
    // lv_obj_set_size(container2, lv_pct(100), lv_pct(100));
    lv_obj_set_width(container2, lv_pct(100));
    lv_obj_set_style_bg_color(container2, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(container2, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(container2, theme_text_color, LV_PART_MAIN);
    lv_obj_set_layout(container2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hint_lable = lv_label_create(container2);
    lv_label_set_text(hint_lable, (std::string(_("Please visit")) + ":").c_str());
    lv_obj_t *hint_lable2 = lv_label_create(container2);
    lv_label_set_text(hint_lable2, "https://wiki.sipeed.com/maixpy/doc/zh/basic/upgrade.html");

    // add upgrade mirror dropdown, PyPi, Aliyun, USTC, Douban, Tsinghua, USTC
    // flex row layout, first is label, second is dropdown list
    // lv_obj_t *mirror_container = lv_obj_create(container);
    // lv_obj_set_size(mirror_container, lv_pct(80), lv_pct(30));
    // lv_obj_set_layout(mirror_container, LV_LAYOUT_FLEX);
    // lv_obj_set_flex_flow(mirror_container, LV_FLEX_FLOW_ROW);
    // lv_obj_set_flex_align(mirror_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // lv_obj_set_style_bg_color(mirror_container, theme_bg_color, LV_PART_MAIN);
    // lv_obj_set_style_border_width(mirror_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_text_color(mirror_container, theme_text_color, LV_PART_MAIN);
    // lv_obj_t *label_mirror = lv_label_create(mirror_container);
    // lv_label_set_text(label_mirror, _("Download from:"));
    // std::string mirrors_str = "";
    // for (auto &mirror : mirrors)
    // {
    //     mirrors_str += mirror + "\n";
    // }
    // ddlist = lv_dropdown_create(mirror_container);
    // lv_obj_set_size(ddlist, lv_pct(60), 50);
    // lv_obj_align(ddlist, LV_ALIGN_CENTER, 0, 0);
    // // lv_obj_set_style_text_font(ddlist, get_font_by_locale(locale), LV_PART_MAIN);
    // // lv_obj_set_style_bg_color(ddlist, theme_btn_color, LV_PART_MAIN);
    // // lv_obj_set_style_text_color(ddlist, lv_color_hex(0xffffff), LV_PART_MAIN);
    // // lv_obj_set_style_border_color(ddlist, theme_btn_color, LV_PART_MAIN);
    // lv_dropdown_set_options(ddlist, mirrors_str.c_str());
    // // change event set mirror_idx
    // lv_obj_add_event_cb(ddlist, [](lv_event_t *e) {
    //     uint32_t code = lv_event_get_code(e);
    //     if (code == LV_EVENT_VALUE_CHANGED)
    //     {
    //         lv_obj_t *ddlist = (lv_obj_t *)lv_event_get_current_target(e);
    //         uint16_t idx = lv_dropdown_get_selected(ddlist);
    //         mirror_idx = idx;
    //     }
    // }, LV_EVENT_VALUE_CHANGED, NULL);

    // upgrading_msg, center and auto wrap, msg may multilines, can scroll
    // lv_obj_t *msg_container = lv_obj_create(container);
    // lv_obj_set_size(msg_container, lv_pct(100), lv_pct(20));
    // lv_obj_set_layout(msg_container, LV_LAYOUT_FLEX);
    // lv_obj_set_flex_flow(msg_container, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_flex_align(msg_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // lv_obj_set_style_bg_color(msg_container, theme_bg_color, LV_PART_MAIN);
    // lv_obj_set_style_border_width(msg_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_pad_all(msg_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_margin_all(msg_container, 0, LV_PART_MAIN);
    // upgrading_msg = lv_label_create(msg_container);
    // lv_label_set_text(upgrading_msg, (_("Strongly recommend directly reflashing the system image!") + std::string("\ngithub.com/sipeed/MaixPy/releases")).c_str());

    // add upgrade button
    btn_upgrade = lv_btn_create(container);
    label_upgrade = lv_label_create(btn_upgrade);
    lv_label_set_text(label_upgrade, _("Loading ..."));
    lv_obj_add_event_cb(btn_upgrade, on_btn_upgrade, LV_EVENT_CLICKED, NULL);
    // set disable mode
    lv_obj_set_state(btn_upgrade, LV_STATE_DISABLED, true);
}

void on_upgrade_maixpy_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    process_should_exit = true;
    process_should_exit2 = true;
    if(load_info_thread)
    {
        log::info("wait thread1");
        while(process_running)
        {
            time::sleep_ms(50);
        }
        load_info_thread->join();
        delete load_info_thread;
        load_info_thread = NULL;
        log::info("wait thread1 done");
    }
    if(load_info_thread2)
    {
        log::info("wait thread2");
        while(process_running2)
        {
            time::sleep_ms(50);
        }
        load_info_thread2->join();
        delete load_info_thread2;
        load_info_thread2 = NULL;
        log::info("wait thread2 done");
    }
}

