#pragma once

#include <map>
#include <string>
#include <vector>

#include "maix_err.hpp"
#include "maix_tensor.hpp"
#include "z_image.hpp"

namespace maix::nn_rk
{
    class MUD
    {
    public:
        MUD(const std::string &model_path = "");
        ~MUD();

        err::Err load(const std::string &model_path);
        err::Err parse_labels(std::vector<std::string> &labels, const std::string key = "labels");
        std::vector<std::string> parse_labels(const std::string key = "labels");

        std::string type;
        std::map<std::string, std::map<std::string, std::string>> items;
        std::string model_path;
    };

    class LayerInfo
    {
    public:
        LayerInfo(const std::string &name = "", tensor::DType dtype = tensor::DType::FLOAT32, std::vector<int> shape = {})
        {
            this->name = name;
            this->dtype = dtype;
            this->shape = shape;
        }

        std::string name;
        tensor::DType dtype;
        std::vector<int> shape;

        std::string to_str() const
        {
            std::string str = "LayerInfo(";
            str += "name='";
            str += name;
            str += "', dtype=";
            str += tensor::dtype_name[dtype];
            str += ", shape=[";
            for(size_t i = 0; i < shape.size(); ++i)
            {
                str += std::to_string(shape[i]);
                if(i + 1 < shape.size())
                {
                    str += ", ";
                }
            }
            str += "])";
            return str;
        }
    };

    class NN
    {
    public:
        NN(const std::string &model = "", bool dual_buff = false);
        ~NN();

        err::Err load(const std::string &model);
        err::Err unload();
        bool loaded();

        void set_dual_buff(bool enable);
        std::vector<LayerInfo> inputs_info();
        std::vector<LayerInfo> outputs_info();
        std::map<std::string, std::string> extra_info();
        std::vector<std::string> extra_info_labels();
        err::Err extra_info_labels(std::vector<std::string> &labels);
        MUD &mud();

        err::Err forward(tensor::Tensors &inputs, tensor::Tensors &outputs, bool copy_result = true, bool dual_buff_wait = false);
        tensor::Tensors *forward(tensor::Tensors &inputs, bool copy_result = true, bool dual_buff_wait = false);
        tensor::Tensors *forward_image(image::Image &img, std::vector<float> mean = {}, std::vector<float> scale = {}, image::Fit fit = image::Fit::FIT_CONTAIN, bool copy_result = true, bool dual_buff_wait = false, bool chw = true);

    private:
        MUD _mud;
        bool _loaded;
        bool _enable_dual_buff;
        void *_data;
    };
}
