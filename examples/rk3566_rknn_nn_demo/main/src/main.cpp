#include "maix_basic.hpp"
#include "maix_nn_rk_classifier.hpp"
#include "maix_image.hpp"

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

    nn_rk::Classifier model(model_path);
    log::info("model loaded: %s", model_path.c_str());
    log::info("input size: %dx%d", model.input_width(), model.input_height());

    image::Image *img = image::load(image_path.c_str(), image::FMT_RGB888);
    if(!img)
    {
        log::error("load image failed: %s", image_path.c_str());
        return -1;
    }

    uint64_t t0 = time::ticks_ms();
    std::vector<std::pair<int, float>> *outs = model.classify(*img, true);
    uint64_t t1 = time::ticks_ms();
    if(!outs)
    {
        delete img;
        log::error("forward_image failed");
        return -1;
    }

    log::info("forward_image done, cost: %d ms", (int)(t1 - t0));
    int top_k = std::min(5, (int)outs->size());
    for(int i = 0; i < top_k; ++i)
    {
        int idx = outs->at(i).first;
        float score = outs->at(i).second;
        log::info("top%d: [%d] %s => %.6f", i + 1, idx, model.class_name(idx).c_str(), score);
    }

    delete outs;
    delete img;
    return 0;
}

int main(int argc, char* argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
