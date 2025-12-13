//
// Created by satuo on 2025/11/16.
//
#include "z_nn_maixcam.hpp"

using namespace maix;

namespace z::nn {
    err::Err maixcam_load_cvimodel(const std::string &model_path, MUD *mud_obj)
    {
        printf(">>> load model_path = %s\n", model_path.c_str());

        return err::ERR_NONE;
    }

    NN_MaixCam::NN_MaixCam(bool dual_buff)
    {
        printf(">>> z_nn_maixcam.cpp NN_MaixCam()\n");

        _init(dual_buff);
    }

    NN_MaixCam::NN_MaixCam()
    {
        _init(true);
    }

    NN_MaixCam::~NN_MaixCam()
    {
    }

    void NN_MaixCam::_init(bool dual_buff)
    {
        printf(">>> z_nn_maixcam.cpp _init(bool)\n");

        _loaded = false;
        _enable_dual_buff = dual_buff;
        _data = nullptr;
    }

    err::Err NN_MaixCam::load(const MUD &mud, const std::string &dir)
    {
        printf(">>> z_nn_maixcam.cpp load()\n");
        _loaded = true;
        return err::ERR_NONE;
    }

    err::Err NN_MaixCam::unload()
    {
        _loaded = false;
        return err::ERR_NONE;
    }

    bool NN_MaixCam::loaded()
    {
        return _loaded;
    }

    void NN_MaixCam::set_dual_buff(bool enable)
    {
        _enable_dual_buff = enable;
    }

    std::vector<LayerInfo> NN_MaixCam::inputs_info()
    {
        return {};
    }

    std::vector<LayerInfo> NN_MaixCam::outputs_info()
    {
        return {};
    }

    err::Err NN_MaixCam::forward(tensor::Tensors &inputs,
                                 tensor::Tensors &outputs,
                                 bool copy_result,
                                 bool dual_buff_wait)
    {
        return err::ERR_NONE;
    }

    tensor::Tensors *NN_MaixCam::forward(tensor::Tensors &inputs,
                                         bool copy_result,
                                         bool dual_buff_wait)
    {
        return nullptr;
    }

    tensor::Tensors *NN_MaixCam::forward_image(image::Image &img,
                                               std::vector<float> mean,
                                               std::vector<float> scale,
                                               image::Fit fit,
                                               bool copy_result,
                                               bool clear_buff,
                                               bool chw)
    {
        return nullptr;
    }

}