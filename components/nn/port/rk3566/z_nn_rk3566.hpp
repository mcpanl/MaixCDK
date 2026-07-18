#pragma once

#include "z_nn.hpp"

namespace maix::nn
{
    class NN_RK3566 : public NNBase
    {
    public:
        NN_RK3566(bool dual_buff = false);
        ~NN_RK3566() override;

        err::Err load(const MUD &mud, const std::string &dir) override;
        err::Err unload() override;
        bool loaded() override;
        void set_dual_buff(bool enable) override;
        std::vector<LayerInfo> inputs_info() override;
        std::vector<LayerInfo> outputs_info() override;
        err::Err forward(tensor::Tensors &inputs, tensor::Tensors &outputs, bool copy_result = true, bool dual_buff_wait = false) override;
        tensor::Tensors *forward(tensor::Tensors &inputs, bool copy_result = true, bool dual_buff_wait = false) override;
        tensor::Tensors *forward_image(image::Image &img, std::vector<float> mean = std::vector<float>(), std::vector<float> scale = std::vector<float>(), image::Fit fit = image::Fit::FIT_CONTAIN, bool copy_result = true, bool dual_buff_wait = false, bool chw = true) override;

    private:
        bool _loaded;
        bool _enable_dual_buff;
        void *_data;
    };
}
