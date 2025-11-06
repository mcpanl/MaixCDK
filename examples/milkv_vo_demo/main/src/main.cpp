#include "maix_basic.hpp"
#include "main.h"
#include "z_lib.hpp"
#include "z_vision_demo.hpp"

using namespace maix;

int _main(int argc, char* argv[])
{
    uint64_t t = time::time_s();
    log::info("Program start 3");

    z::z_lib_init();

//    z::z_vision_demo_init();

//    display::Display *disp = new display::Display(552, 368, image::FMT_BGRA8888);

//    log::info("disp: %p", disp);

    // Run until app want to exit, for example app::switch_app API will set exit flag.
    // And you can also call app::set_exit_flag(true) to mark exit.
    VIDEO_FRAME_INFO_S stFrameInfo;

    while(!app::need_exit())
    {
        log::info("%d", time::time_s());

        z::z_lib_vpss_take_frame(&stFrameInfo, 1000);
//        z::z_lib_vi_take_frame(&stFrameInfo, 1000);

        printf("Got frame: %ux%u\n", stFrameInfo.stVFrame.u32Width, stFrameInfo.stVFrame.u32Height);

        z::z_lib_vo_push_frame(&stFrameInfo);

//        z::z_lib_vi_release_frame(&stFrameInfo);
        z::z_lib_vpss_release_frame(&stFrameInfo);

        time::sleep(1);

        if(time::time_s() - t > 10)
            app::set_exit_flag(true);
    }

    z::z_lib_deinit();

    log::info("Program exit");

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


