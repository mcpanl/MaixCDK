#include "maix_basic.hpp"
#include "maix_image.hpp"
#include "maix_nn_rk_yolo.hpp"

using namespace maix;

static int _main(int argc, char *argv[])
{
    if(argc < 3)
    {
        log::error("Usage: %s <model.rknn|model.mud> <image>", argv[0]);
        return -1;
    }

    const std::string model_path = argv[1];
    const std::string image_path = argv[2];

    nn_rk::YOLOv5 detector(model_path);
    log::info("model loaded: %s", model_path.c_str());
    log::info("input size: %dx%d", detector.input_width(), detector.input_height());

    image::Image *img = image::load(image_path.c_str(), image::FMT_RGB888);
    if(!img)
    {
        log::error("load image failed: %s", image_path.c_str());
        return -1;
    }

    const float conf_th = 0.25f;
    const float iou_th = 0.45f;

    uint64_t t0 = time::ticks_ms();
    std::vector<nn_rk::DetectObject> *outs = detector.detect(*img, conf_th, iou_th, image::FIT_CONTAIN);
    uint64_t t1 = time::ticks_ms();
    if(!outs)
    {
        delete img;
        log::error("detect failed");
        return -1;
    }

    log::info("detect done, cost: %d ms, objects: %d", (int)(t1 - t0), (int)outs->size());
    for(size_t i = 0; i < outs->size(); ++i)
    {
        const nn_rk::DetectObject &obj = outs->at(i);
        const char *cls_name = (obj.class_id >= 0 && obj.class_id < (int)detector.labels.size())
                                   ? detector.labels[obj.class_id].c_str()
                                   : "unknown";
        log::info("det[%d]: class=%d(%s), score=%.4f, xywh=(%.2f, %.2f, %.2f, %.2f)",
                  (int)i, obj.class_id, cls_name, obj.score, obj.x, obj.y, obj.w, obj.h);
        img->draw_rect((int)obj.x, (int)obj.y, (int)obj.w, (int)obj.h, image::Color::from_rgb(255, 0, 0), 2);
        img->draw_string((int)obj.x, (int)obj.y, cls_name, image::Color::from_rgb(255, 0, 0));
    }
    img->save("result.jpg");
    log::info("result saved to result.jpg");

    delete outs;
    delete img;
    return 0;
}

int main(int argc, char *argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
