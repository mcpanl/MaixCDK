
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
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>

#include "maix_basic.hpp"
#include "run_app.hpp"
#include "lvgl.h"
#include "gui.hpp"
#include "z_display.hpp"
#include "z_touchscreen.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <cctype>
#include <algorithm>
#include <thread>
#include "maix_comm.hpp"


static Maix_GUI_Activity *main_activity = NULL;

// static std::string strip(const std::string& str) {
//     auto left = std::find_if_not(str.begin(), str.end(), [](unsigned char ch) { return std::isspace(ch); });
//     auto right = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
//     if (right <= left) {
//         return ""; // All spaces or empty string
//     }
//     return std::string(left, right);
// }

void main_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    vector<app::APP_Info> &apps_info = app::get_apps_info(true, false);
    /*MAIX_GUI_OBJ_T* obj = */ launcher::create_home_items(activity, apps_info, obj);
}

void on_destroy_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    vector<app::APP_Info> &apps_info = app::get_apps_info();
    /*MAIX_GUI_OBJ_T* obj = */ launcher::home_items_free(activity, apps_info, obj);
}

void open_fb_show_info(const std::string &msg, const std::string &msg2 = "")
{
    const std::string insmod_ko = "insmod /mnt/system/ko/soph_fb.ko";
    int ret = system(insmod_ko.c_str());
    if(ret != 0)
    {
        log::warn("insmod /mnt/system/ko/soph_fb.ko failed");
    }

    int fbfd = 0;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    long int screensize = 0;
    unsigned char *fbp = 0;

    // 打开 framebuffer 设备
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        log::error("Error: cannot open framebuffer device");
        return;
    }

    log::info("/dev/fb0 opened");

    // 获取固定屏幕信息
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        log::error("Error reading fixed information");
        return;
    }

    log::info("got fb width: %d", finfo.line_length / 4);

    // 获取可变屏幕信息
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        log::error("Error reading variable information");
        return;
    }
    log::info("got fb height: %d", vinfo.yres_virtual / 2);

    // 映射 framebuffer 到内存
    screensize = vinfo.yres_virtual * finfo.line_length;
    fbp = (unsigned char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if ((int64_t)fbp == -1) {
        log::error("Error: failed to map framebuffer device to memory");
        close(fbfd);
        return;
    }

    log::info("got fb data");

    uint64_t t = time::ticks_s();
    image::Image *img = new image::Image(finfo.line_length / 4, vinfo.yres_virtual / 2, image::Format::FMT_BGRA8888, fbp, screensize / 2, false);
    if(!img)
    {
        log::error("malloc image failed");
        close(fbfd);
        return;
    }
    while(time::ticks_s() - t < 20)
    {
        img->draw_rect(0, 0, img->width(), img->height(), image::Color::from_rgb(255, 0, 0), -1);
        img->draw_string(4, 32, msg, image::Color::from_rgb(255, 255, 255), 1.3);
        if(!msg2.empty())
        {
            img->draw_string(4, 128, msg2, image::Color::from_rgb(255, 255, 255), 1);
        }
        std::string msg3 = "exit in " + std::to_string(20 - (int)(time::ticks_s() - t)) + "s";
        img->draw_string(4, 256, msg3, image::Color::from_rgb(255, 255, 255), 1);
        time::sleep(4);
    }
    delete img;
}

#define APP_CMD_ECHO 0x01
#define APP_ID "my_app1"
#define BUFF_RX_LEN 1024

int _main(int argc, char **argv)
{
    int ret;

    printf("----- launcher start -----\n");

#if 0
    display::Display *disp = new display::Display(640, 480);
    image::Image *img = new image::Image(640, 480);
    img->draw_string(28, 28, "Launcher", image::COLOR_RED, 3, 2);
    disp->show(*img);
    time::sleep(1);

    delete img;
    img = nullptr;

    delete disp;
    z_lib_deinit();

    log::info("Program exit");
#endif

    printf("arg len: %d\n", argc);
    for (int i = 0; i < argc; i++)
    {
        printf("arg %d: %s\n", i, argv[i]);
    }
    launcher::g_app_exec_path = argv[0];
    // if (argc == 3 && strstr(argv[1], "daemon") != NULL)
    // {
    //     int app_pid = atoi(argv[2]);
    //     printf("launcher daemon process start, app pid: %d\n", app_pid);
    //     // wait until app exit
    //     int status;
    //     waitpid(app_pid, &status, 0);
    //     printf("\n-- app exit, status: %d\n\n", status);
    // }

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

    // if (argc == 1) // no args(daemon start will have 2 args)
    // {
    //     std::string auto_start_id = get_curr_auto_start_app();
    //     log::info("auto start id: %s", auto_start_id.c_str());
    //     if (!auto_start_id.empty())
    //     {
    //         vector<app::APP_Info> &apps = app::get_apps_info(true, true);
    //         for (auto &info : apps)
    //         {
    //             log::info("id: %s, %s", info.id.c_str(), auto_start_id.c_str());
    //             if (info.id == auto_start_id)
    //             {
    //                 std::string exec_abs_path = app::get_app_path(auto_start_id) + "/" + info.exec;
    //                 launcher::run_app(exec_abs_path.c_str(), launcher::g_app_exec_path, info.id.c_str());
    //                 throw err::Exception(err::ERR_NOT_PERMIT); // never here
    //             }
    //         }
    //     }
    // }

    // init display
    display::Display *screen = nullptr;
    try
    {
        screen = new display::Display(640, 480);
    }
    catch (...)
    {
        log::error("open display failed");
        open_fb_show_info("launcher open display failed", "Please refer to doc's FAQ");
        throw err::Exception(err::ERR_RUNTIME, "Open display failed");
    }
    try
    {
        std::string value_str = app::get_sys_config_kv("backlight", "value", "50");
        // value to float
        float value_float = atof(value_str.c_str());
        screen->set_backlight(value_float);

        // touch screen
        touchscreen::TouchScreen touchscreen_ = touchscreen::TouchScreen();

        Maix_GUI<MAIX_GUI_OBJ_T> gui(false);
        ret = gui.init(screen, &touchscreen_);
        if (ret != 0)
        {
            log::error("gui init failed\n");
            return 1;
        }

        main_activity = gui.get_main_activity();
        main_activity->set_init_ui_cb(main_gui);
        main_activity->set_destroy_cb(on_destroy_gui);
        main_activity->active();

        gui.run();
    }
    catch (const std::exception& e)
    {
        log::error("launcher run exception: %s", e.what());
        open_fb_show_info("launcher run exception: " + std::string(e.what()));
        delete screen;
        throw err::Exception(err::ERR_RUNTIME, e.what());
    }
    catch (...)
    {
        log::error("launcher run unknown exception");
        open_fb_show_info("launcher run unknown exception");
        delete screen;
        throw err::Exception(err::ERR_RUNTIME, "lancher unknown exception");
    }
    delete screen;
    return 0;
}

int main(int argc, char *argv[])
{
    // Catch SIGINT signal(e.g. Ctrl + C), and set exit flag to true.
    signal(SIGINT, [](int sig)
           { app::set_exit_flag(true); });

    // Use CATCH_EXCEPTION_RUN_RETURN to catch exception,
    // if we don't catch exception, when program throw exception, the objects will not be destructed.
    // So we catch exception here to let resources be released(call objects' destructor) before exit.
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
