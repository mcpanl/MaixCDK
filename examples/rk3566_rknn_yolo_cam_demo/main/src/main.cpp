#include "main.h"
#include "maix_basic.hpp"
#include "maix_nn_rk_yolo.hpp"
#include "z_camera.hpp"
#include "z_display.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

using namespace maix;

static std::string model_path_from_home()
{
    const char *home = std::getenv("HOME");
    if(home && home[0] != '\0')
        return std::string(home) + "/models/yolov5n_224.mud";
    return std::string("/models/yolov5n_224.mud");
}

static void print_usage(const char *prog)
{
    log::info("Usage: %s [DEVICE]", prog ? prog : "rk3566_rknn_yolo_cam_demo");
    log::info("  Model: ~/models/yolov5n_224.mud (from $HOME)");
    log::info("  (no args)  default camera");
    log::info("  DEVICE     e.g. /dev/video0");
}

static int _main(int argc, char *argv[])
{
    const char *prog = (argc > 0 && argv[0]) ? argv[0] : "rk3566_rknn_yolo_cam_demo";

    if(argc >= 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)) {
        print_usage(prog);
        return 0;
    }

    const std::string model_path = model_path_from_home();
    const char *cam_dev = nullptr;
    if(argc >= 2 && argv[1][0] != '-')
        cam_dev = argv[1];

    nn_rk::YOLOv5 detector(model_path);
    log::info("model: %s", model_path.c_str());
    log::info("input: %dx%d", detector.input_width(), detector.input_height());

    camera::Camera cam(640, 480, image::FMT_RGB888, cam_dev, 30, 3, true, false);
    display::Display disp(cam.width(), cam.height(), image::FMT_RGB888, "", true);

    const float conf_th = 0.25f;
    const float iou_th = 0.45f;

    log::info("Camera -> YOLO -> Display. Close window to exit.");

    while(!app::need_exit()) {
        image::Image *frame = cam.read(nullptr, 0, true, 500);
        if(!frame)
            continue;

        uint64_t t0 = time::ticks_ms();
        std::vector<nn_rk::DetectObject> *outs = detector.detect(*frame, conf_th, iou_th, image::FIT_CONTAIN);
        uint64_t t1 = time::ticks_ms();
        if(!outs) {
            delete frame;
            continue;
        }

        frame->draw_string(8, 8,
                           std::string("YOLO ") + std::to_string((int)outs->size()) + " objs, " +
                               std::to_string((int)(t1 - t0)) + " ms",
                           image::Color::from_rgb(0, 255, 64), 1.0f, 2);

        for(size_t i = 0; i < outs->size(); ++i) {
            const nn_rk::DetectObject &obj = outs->at(i);
            const char *cls_name = (obj.class_id >= 0 && obj.class_id < (int)detector.labels.size())
                                       ? detector.labels[obj.class_id].c_str()
                                       : "unknown";
            frame->draw_rect((int)obj.x, (int)obj.y, (int)obj.w, (int)obj.h, image::Color::from_rgb(255, 64, 64), 2);
            frame->draw_string((int)obj.x, (int)obj.y, cls_name, image::Color::from_rgb(255, 220, 64));
        }

        err::Err e = disp.show(*frame);
        delete outs;
        delete frame;
        if(e != err::ERR_NONE) {
            log::error("disp.show failed: %d", (int)e);
            break;
        }

        disp.poll_events();
        if(!disp.is_opened()) {
            log::info("Window closed");
            break;
        }
    }

    cam.close();
    return 0;
}

int main(int argc, char *argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
