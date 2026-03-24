
#include "maix_basic.hpp"
#include "z_nn.hpp"
#include "z_image.hpp"
#include "main.h"
// #include "z_nn_yolov5.hpp"
// #include "z_nn_classifier.hpp"
#include "z_nn_face_detector.hpp"

#include "z_touchscreen.hpp"
#include "z_display.hpp"
#include "z_camera.hpp"
#include "maix_key.hpp"

using namespace maix;
using namespace maix::peripheral;

static void on_key(int key, int state)
{
    log::info("key: %d, state: %d", key, state);
    if (state == 0)  // KEY_RELEASED
    {
        app::set_exit_flag(true);
    }
}

int _main(int argc, char* argv[])
{
    err::Err e;
    char tmp_chars[64] = {0};
    uint64_t t = time::time_s();
    log::info("Program start");


    display::Display *disp = new display::Display(640, 480);
    printf("disp = %p\n", disp);

    camera::Camera *cam = new camera::Camera(640, 480);

    image::Image* img = nullptr;
    img = new image::Image(640, 480);
    img->draw_string(28, 28, "NN Face Demo", image::COLOR_RED, 3, 2);
    disp->show(*img);

    if(img) {
        delete img;
        img = nullptr;
    }

    time::sleep(1);

#if 1
    string model_path = "/root/models/face_detector.mud";

    nn::FaceDetector faceDetector("", false);
    e = faceDetector.load(model_path);
    err::check_raise(e, "load model failed");


    while (!app::need_exit())
    {

        img = cam->read();
        if (img) {

                
            std::vector<nn::Object> * result = faceDetector.detect(*img);

            if (result)
            {
                for (auto &obj : *result)
                {
                    log::info(obj.to_str().c_str());

                    img->draw_rect(obj.x, obj.y, obj.w, obj.h, image::COLOR_GREEN, 4);
                }
            }

            disp->show(*img);

            delete result;
            delete img;
            img = nullptr;
            log::info("--------");
        }

        time::sleep_ms(33);
    }
    

#endif

#if 0

    string model_path = "/root/models/mobilenetv2.mud";

    // nn::NN model(model_path);

    nn::Classifier classifier("", false);
    e = classifier.load(model_path);
    err::check_raise(e, "load model failed");
    log::info("load mobilenetv2 model %s success", model_path.c_str());

    key::Key key(on_key);
    touchscreen::TouchScreen touch("", false);
    if (touch.open() != err::ERR_NONE)
    {
        log::warn("touchscreen open failed, touch exit disabled");
    }
    else
    {
        log::info("touchscreen init ok, touch top-left corner to exit");
    }

    if(img) {
        delete img;
        img = nullptr;
    }

    const int EXIT_AREA_W = 80;
    const int EXIT_AREA_H = 60;

    while (!app::need_exit())
    {
        if (touch.is_opened())
        {
            auto touch_status = touch.read();
            if (touch_status.size() >= 3 && touch_status[2] &&
                touch_status[0] < EXIT_AREA_W && touch_status[1] < EXIT_AREA_H)
        {
                log::info("touch exit area, exiting");
                app::set_exit_flag(true);
            }
        }

        img = cam->read();
        if (img) {


            std::vector<std::pair<int, float>> * result = classifier.classify(*img);

            std::string label = classifier.labels[result->at(0).first];

            img->draw_string(28, 28, label, image::COLOR_RED, 2, 2);
            img->draw_rect(0, 0, EXIT_AREA_W, EXIT_AREA_H, image::COLOR_WHITE, 2);
            img->draw_string(4, 20, "Exit", image::COLOR_WHITE, 1.5, 1);

            for(int i = 0; i < 3; ++i)
            {
                log::info("top %d: %5.2f%% %s, ", i + 1, result->at(i).second * 100, classifier.labels[result->at(i).first].c_str());
            }
            disp->show(*img);

            delete result;
            delete img;
            img = nullptr;
            log::info("--------");
        }

        time::sleep_ms(33);
    }
    


#endif

#if 0
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

    if(cam) {
        delete cam;
    }

    if(disp) {
        delete disp;
    }

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


