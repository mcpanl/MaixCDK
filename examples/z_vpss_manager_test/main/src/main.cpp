#include "maix_basic.hpp"
#include "z_camera.hpp"
#include "z_display.hpp"
#include "z_nn_yolov5.hpp"

using namespace maix;

int _main(int argc, char *argv[])
{
    printf("=== VPSS Manager Debug Demo ===\n\n");
    char tmp_chars[64] = {0};

    // 创建显示
    printf("\n>> Opening Display...\n");
    display::Display disp(640, 480, image::FMT_RGB888);

    // 创建摄像头
    printf("\n>> Opening Camera...\n");
    camera::Camera cam(640, 480, image::FMT_RGB888);

    camera::Camera *cam2 = cam.add_channel(320, 240, image::FMT_RGB888);


    nn::YOLOv5 detector("", false);
    err::Err e = detector.load("/root/models/yolov5s.mud");
    err::check_raise(e, "load model failed");
    log::info("load yolov5 model /root/models/yolov5s.mud success");



    // 读取一些帧
    printf("\n>> Reading frames...\n");
    for (int i = 0; i < 20; i++) {
        image::Image* img = cam.read();
        if (img) {
            printf("  Frame %d: %dx%d\n", i+1, img->width(), img->height());
            img->draw_string(28, 28, "1Hello, world! " + to_string(i), image::COLOR_RED, 3, 2);
            // 显示到屏幕
            disp.show(*img);

            delete img;
        }

        time::sleep_ms(500);

        // image::Image* img = cam.read();
        // // image::Image* img = new image::Image(640, 480);
        // img->draw_string(28, 28, "1Hello, world! " + to_string(i), image::COLOR_RED, 3, 2);
        // disp.show(*img);
        // delete img;

        image::Image* img2 = cam2->read();

        if (img2) {
            printf("  Frame %d: %dx%d\n", i+1, img2->width(), img2->height());

            log::info("detect now");
            std::vector<nn::Object> *result = detector.detect(*img2, 0.4, 0.5);
            for (auto &r : *result)
            {
                log::info("result: %s, %s", r.to_str().c_str(), detector.labels[r.class_id].c_str());
                img2->draw_rect(r.x, r.y, r.w, r.h, maix::image::Color::from_rgb(255, 0, 0));
                snprintf(tmp_chars, sizeof(tmp_chars), "%s: %.2f", detector.labels[r.class_id].c_str(), r.score);
                img2->draw_string(r.x, r.y, tmp_chars, maix::image::Color::from_rgb(255, 0, 0));
            }

            img2->draw_string(28, 28, "1Hello, world! " + to_string(i), image::COLOR_RED, 1, 1);
            disp.show(*img2);
            delete img2;
            delete result;
        }

        log::info("i = %d", i);
        time::sleep_ms(500);
    }

    delete cam2;

    printf("\n=== Demo Complete ===\n");
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


