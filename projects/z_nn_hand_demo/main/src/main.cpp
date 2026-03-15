
#include "maix_basic.hpp"
#include "main.h"
#include "z_lib.hpp"
#include "z_display.hpp"
#include "z_camera.hpp"
#include "z_image.hpp"
#include "z_nn_test_hand.hpp"
#include "z_nn_object.hpp"
#include "maix_key.hpp"

using namespace maix;
using namespace maix::peripheral;

static std::string handClassName(int id)
{
    if (id == 0)
        return "hand";
    return "hand_" + std::to_string(id);
}

void cb(int a, int b)
{
    printf("cb %d %d\n", a, b);
    if (b == 0)
    {
        app::set_exit_flag(true);
    }
}

int _main(int argc, char* argv[])
{
    log::info("Program start");

    key::Key key(cb);

    nn::Object obj(10, 20, 100, 50);
    printf("Object = %s\n", obj.to_str().c_str());

    display::Display *disp = new display::Display(640, 480);
    image::Image *img = new image::Image(640, 480);
    img->draw_string(28, 28, "Hand Demo", image::COLOR_RED, 3, 2);
    disp->show(*img);
    time::sleep(1);

    delete img;
    img = nullptr;

    camera::Camera *cam = new camera::Camera(640, 480);

    hand_ctx_t hand_ctx;
    if (hand_init(&hand_ctx) != CVI_SUCCESS) {
        printf("HAND init failed\n");
    }

    while(!app::need_exit())
    {
        image::Image *cam_img = cam->read();
        if (!cam_img) {
            break;
        }

        cvtdl_object_t obj_meta;
        memset(&obj_meta, 0, sizeof(obj_meta));
        hand_detect(&hand_ctx, &obj_meta);

        for (uint32_t i = 0; i < obj_meta.size; i++) {
            float x1 = obj_meta.info[i].bbox.x1;
            float y1 = obj_meta.info[i].bbox.y1;
            float x2 = obj_meta.info[i].bbox.x2;
            float y2 = obj_meta.info[i].bbox.y2;

            float w = x2 - x1;
            float h = y2 - y1;
            if (w <= 0 || h <= 0) {
                continue;
            }

            std::string class_name = handClassName(obj_meta.info[i].classes);
            char score_text[32] = {0};
            snprintf(score_text, sizeof(score_text), "%s %.2f", class_name.c_str(), obj_meta.info[i].bbox.score);

            cam_img->draw_rect(
                (int)x1, (int)y1,
                (int)w,  (int)h,
                image::COLOR_RED, 3
            );

            int text_y = (int)y1 - 26;
            if (text_y < 0) {
                text_y = (int)y1 + 4;
            }
            cam_img->draw_string((int)x1, text_y, score_text, image::COLOR_RED, 2, 2);
        }

        CVI_TDL_Free(&obj_meta);

        disp->show(*cam_img);
        delete cam_img;

        time::sleep_ms(33);
    }

    hand_deinit(&hand_ctx);

    delete cam;
    delete disp;
    z_lib_deinit();

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


