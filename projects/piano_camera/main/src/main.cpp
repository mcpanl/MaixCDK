#include "stdio.h"
#include "main.h"
#include "maix_util.hpp"
#include "maix_image.hpp"
#include "maix_time.hpp"
#include "maix_display.hpp"
#include "maix_rtsp.hpp"
#include "maix_camera.hpp"
#include "maix_basic.hpp"
#include "csignal"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

using namespace maix;


static struct {
    camera::Camera *cam;
    camera::Camera *cam2;
    rtsp::Rtsp *rtsp;
} priv;



const char* get_current_time_string() {
    static char buffer[20];
    ::time_t rawtime;
    ::tm *timeinfo;

    ::time(&rawtime);
    timeinfo = ::localtime(&rawtime);

    ::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return buffer;
}

int _main(int argc, char* argv[])
{
    int cam_w = 1280;
    int cam_h = 720;
    int cam2_w = 640;
    int cam2_h = 480;

    image::Format cam_fmt = image::Format::FMT_YVU420SP;
    int cam_fps = 30;
    int cam_buffer_num = 3;

    priv.cam = new camera::Camera(cam_w, cam_h, cam_fmt, "", cam_fps, cam_buffer_num);
    priv.cam2 = priv.cam->add_channel(cam2_w, cam2_h, cam_fmt, cam_fps, cam_buffer_num);

    display::Display disp = display::Display();
    auto audio_recorder = audio::Recorder();
    priv.rtsp = new rtsp::Rtsp();
    priv.rtsp->bind_camera(priv.cam2);
    priv.rtsp->bind_audio_recorder(&audio_recorder);
    rtsp::Region *region = priv.rtsp->add_region(0, 0, 256, 64);

    image::Image *rgn_img;

    log::info("url:%s", priv.rtsp->get_url().c_str());
    std::vector<std::string> url = priv.rtsp->get_urls();
    for (size_t i = 0; i < url.size(); i ++) {
        log::info("url[%d]:%s", i, url[i].c_str());
    }
    err::check_raise(priv.rtsp->start());

    uint64_t last_ms = time::ticks_ms();
    int cnt = 0;
    while(!app::need_exit()) {
        rgn_img = region->get_canvas();
        const char* time_str = get_current_time_string();
        rgn_img->draw_string(24, 24, time_str, image::COLOR_WHITE);
        region->update_canvas();
        delete rgn_img;

        maix::image::Image *img = nullptr;
        try {
            img = priv.cam2->read();
        } catch (std::exception &e) {
            time::sleep_ms(10);
            continue;
        }

        disp.show(*img);
        delete img;
        uint64_t curr_ms = time::ticks_ms();
        log::info("loop use %lld ms\r\n", curr_ms - last_ms);
        last_ms = curr_ms;
    }

    priv.rtsp->stop();
    delete priv.rtsp;
    delete priv.cam2;
    delete priv.cam;
    return 0;
}

int main(int argc, char* argv[])
{
    // Catch signal and process
    sys::register_default_signal_handle();

    // Use CATCH_EXCEPTION_RUN_RETURN to catch exception,
    // if we don't catch exception, when program throw exception, the objects will not be destructed.
    // So we catch exception here to let resources be released(call objects' destructor) before exit.
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
