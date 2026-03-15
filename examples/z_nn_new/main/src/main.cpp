
#include "maix_basic.hpp"
#include "z_nn.hpp"
#include "z_image.hpp"
#include "main.h"
#include "z_nn_yolov5.hpp"

using namespace maix;

int _main(int argc, char* argv[])
{
    err::Err e;
    char tmp_chars[64] = {0};
    uint64_t t = time::time_s();
    log::info("Program start");

#if 0
    char* img_path = "/root/cat2.jpg";
    maix::image::Image *img = maix::image::load(img_path, maix::image::FMT_RGB888);
    err::check_null_raise(img, "load image " + std::string(img_path) + " failed");
    log::info("load image %s success: %s", img_path, img->to_str().c_str());

    log::info("will resize image");

    maix::image::Image *resized = img->resize(224, 224, maix::image::FIT_COVER);
    log::info("resize image %s success: %s", img_path, resized->to_str().c_str());

    delete img;
    delete resized;
#endif

#if 1
    string model_path = "/root/models/yolov5s.mud";

    // nn::NN model(model_path);

    nn::YOLOv5 detector("", false);
    e = detector.load(model_path);
    err::check_raise(e, "load model failed");
    log::info("load yolov5 model %s success", model_path.c_str());


    log::info("load image now");
    char* img_path = "/root/cat2.jpg";
    maix::image::Image *img = maix::image::load(img_path, maix::image::FMT_RGB888);
    err::check_null_raise(img, "load image " + std::string(img_path) + " failed");
    log::info("load image %s success: %s", img_path, img->to_str().c_str());

    log::info("detect now");
    std::vector<nn::Object> *result = detector.detect(*img, 0.45, 0.5);

    for (auto &r : *result)
    {
        log::info("result: %s, %s", r.to_str().c_str(), detector.labels[r.class_id].c_str());
        log::info("draw result on image");
        img->draw_rect(r.x, r.y, r.w, r.h, maix::image::Color::from_rgb(255, 0, 0));
        log::info("draw label and score on image");
        snprintf(tmp_chars, sizeof(tmp_chars), "%s: %.2f", detector.labels[r.class_id].c_str(), r.score);
        img->draw_string(r.x, r.y, tmp_chars, maix::image::Color::from_rgb(255, 0, 0));
    }

    log::info("detect finished, now save image with result");

    img->save("result.jpg");
    delete result;
    delete img;

    // Run until app want to exit, for example app::switch_app API will set exit flag.
    // And you can also call app::set_exit_flag(true) to mark exit.
    while(!app::need_exit())
    {
        log::info("%d", time::time_s());
        time::sleep(1);

        if(time::time_s() - t > 6)
            app::set_exit_flag(true);
    }
#endif
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


