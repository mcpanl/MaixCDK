#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "maix_fs.hpp"
#include "maix_log.hpp"
#include "z_nn_rk.hpp"

namespace maix::nn_rk
{
    class Classifier
    {
    public:
        Classifier(const std::string &model = "", bool dual_buff = true)
        {
            _model = nullptr;
            _dual_buff = dual_buff;
            _chw = true;
            _input_img_fmt = image::FMT_RGB888;
            if(!model.empty())
            {
                err::Err e = load(model);
                if(e != err::ERR_NONE)
                {
                    throw err::Exception(e, "load model failed");
                }
            }
        }

        ~Classifier()
        {
            if(_model)
            {
                delete _model;
                _model = nullptr;
            }
        }

        err::Err load(const std::string &model)
        {
            if(_model)
            {
                delete _model;
                _model = nullptr;
            }
            labels.clear();
            mean.clear();
            scale.clear();
            label_path.clear();

            _model = new nn_rk::NN(model, _dual_buff);
            if(!_model)
            {
                return err::ERR_NO_MEM;
            }

            _extra_info = _model->extra_info();
            auto it_model_type = _extra_info.find("model_type");
            if(it_model_type == _extra_info.end() || it_model_type->second != "classifier")
            {
                log::error("model_type should be classifier");
                return err::ERR_ARGS;
            }

            auto it_input_type = _extra_info.find("input_type");
            if(it_input_type != _extra_info.end())
            {
                std::string input_type = it_input_type->second;
                if(input_type == "rgb") _input_img_fmt = image::FMT_RGB888;
                else if(input_type == "bgr") _input_img_fmt = image::FMT_BGR888;
                else if(input_type == "gray") _input_img_fmt = image::FMT_GRAYSCALE;
                else
                {
                    log::error("unsupported input_type: %s", input_type.c_str());
                    return err::ERR_ARGS;
                }
            }

            if(_extra_info.find("mean") != _extra_info.end())
            {
                if(_parse_float_list(_extra_info["mean"], mean) != err::ERR_NONE) return err::ERR_ARGS;
            }
            if(_extra_info.find("scale") != _extra_info.end())
            {
                if(_parse_float_list(_extra_info["scale"], scale) != err::ERR_NONE) return err::ERR_ARGS;
            }
            if(_extra_info.find("input_channel") != _extra_info.end())
            {
                if(_extra_info["input_channel"] == "hwc") _chw = false;
            }

            err::Err e = _model->extra_info_labels(labels);
            if(e != err::ERR_NONE)
            {
                log::warn("labels not found in mud extra info");
            }
            else
            {
                if(_extra_info.find("labels") != _extra_info.end())
                {
                    label_path = fs::dirname(_model->mud().model_path) + "/" + _extra_info["labels"];
                }
            }

            _inputs = _model->inputs_info();
            if(_inputs.empty())
            {
                return err::ERR_ARGS;
            }
            if(_inputs[0].shape.size() == 4)
            {
                if(_inputs[0].shape[1] == 3 || _inputs[0].shape[1] == 1)
                    _input_size = image::Size(_inputs[0].shape[3], _inputs[0].shape[2]);
                else
                    _input_size = image::Size(_inputs[0].shape[2], _inputs[0].shape[1]);
            }
            else if(_inputs[0].shape.size() == 3)
            {
                if(_inputs[0].shape[0] == 3 || _inputs[0].shape[0] == 1)
                    _input_size = image::Size(_inputs[0].shape[2], _inputs[0].shape[1]);
                else
                    _input_size = image::Size(_inputs[0].shape[1], _inputs[0].shape[0]);
            }
            else
            {
                return err::ERR_ARGS;
            }
            return err::ERR_NONE;
        }

        std::vector<std::pair<int, float>> *classify(image::Image &img, bool softmax = true, image::Fit fit = image::FIT_FILL)
        {
            image::Image *input = &img;
            image::Image *converted = nullptr;
            if(img.format() != _input_img_fmt)
            {
                converted = img.to_format(_input_img_fmt);
                if(!converted)
                {
                    throw err::Exception(err::ERR_RUNTIME, "convert image format failed");
                }
                input = converted;
            }

            tensor::Tensors *outputs = _model->forward_image(*input, mean, scale, fit, true, false, _chw);
            if(converted) delete converted;
            if(!outputs)
            {
                return nullptr;
            }
            if(outputs->size() == 0)
            {
                delete outputs;
                return nullptr;
            }

            tensor::Tensor *tensor = outputs->begin()->second;
            std::vector<float> scores;
            _tensor_to_scores(*tensor, scores);
            if(softmax)
            {
                _softmax(scores);
            }

            std::vector<std::pair<int, float>> *result = new std::vector<std::pair<int, float>>();
            result->reserve(scores.size());
            for(size_t i = 0; i < scores.size(); ++i)
            {
                result->push_back({(int)i, scores[i]});
            }
            std::sort(result->begin(), result->end(), [](const auto &a, const auto &b) { return a.second > b.second; });
            delete outputs;
            return result;
        }

        std::vector<std::pair<std::string, float>> *classify_with_names(image::Image &img, int top_k = 5, bool softmax = true, image::Fit fit = image::FIT_FILL)
        {
            std::vector<std::pair<int, float>> *raw = classify(img, softmax, fit);
            if(!raw)
            {
                return nullptr;
            }
            if(top_k <= 0) top_k = 1;
            std::vector<std::pair<std::string, float>> *named = new std::vector<std::pair<std::string, float>>();
            int n = std::min((int)raw->size(), top_k);
            named->reserve(n);
            for(int i = 0; i < n; ++i)
            {
                int idx = raw->at(i).first;
                named->push_back({class_name(idx), raw->at(i).second});
            }
            delete raw;
            return named;
        }

        std::string class_name(int idx) const
        {
            if(idx >= 0 && idx < (int)labels.size()) return labels[idx];
            return std::to_string(idx);
        }

        image::Size input_size() { return _input_size; }
        int input_width() { return _input_size.width(); }
        int input_height() { return _input_size.height(); }
        image::Format input_format() { return _input_img_fmt; }

    public:
        std::vector<std::string> labels;
        std::string label_path;
        std::vector<float> mean;
        std::vector<float> scale;

    private:
        static err::Err _parse_float_list(const std::string &s, std::vector<float> &out)
        {
            out.clear();
            size_t start = 0;
            size_t end = s.find(',');
            while(end != std::string::npos)
            {
                std::string token = s.substr(start, end - start);
                token.erase(0, token.find_first_not_of(" \t\r\n"));
                token.erase(token.find_last_not_of(" \t\r\n") + 1);
                if(!token.empty()) out.push_back(std::stof(token));
                start = end + 1;
                end = s.find(',', start);
            }
            std::string token = s.substr(start);
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            token.erase(token.find_last_not_of(" \t\r\n") + 1);
            if(!token.empty()) out.push_back(std::stof(token));
            return err::ERR_NONE;
        }

        static float _fp16_to_float(uint16_t h)
        {
            uint32_t sign = (h & 0x8000u) << 16;
            uint32_t exp = (h & 0x7C00u) >> 10;
            uint32_t mant = (h & 0x03FFu);

            uint32_t f;
            if(exp == 0)
            {
                if(mant == 0)
                {
                    f = sign;
                }
                else
                {
                    exp = 127 - 15 + 1;
                    while((mant & 0x0400u) == 0)
                    {
                        mant <<= 1;
                        --exp;
                    }
                    mant &= 0x03FFu;
                    f = sign | (exp << 23) | (mant << 13);
                }
            }
            else if(exp == 0x1Fu)
            {
                f = sign | 0x7F800000u | (mant << 13);
            }
            else
            {
                exp = exp + (127 - 15);
                f = sign | (exp << 23) | (mant << 13);
            }
            float out;
            memcpy(&out, &f, sizeof(out));
            return out;
        }

        static void _tensor_to_scores(tensor::Tensor &t, std::vector<float> &scores)
        {
            int n = t.size_int();
            scores.resize(n);
            if(t.dtype() == tensor::DType::FLOAT32)
            {
                float *p = (float *)t.data();
                for(int i = 0; i < n; ++i) scores[i] = p[i];
            }
            else if(t.dtype() == tensor::DType::FLOAT16)
            {
                uint16_t *p = (uint16_t *)t.data();
                for(int i = 0; i < n; ++i) scores[i] = _fp16_to_float(p[i]);
            }
            else if(t.dtype() == tensor::DType::INT8)
            {
                int8_t *p = (int8_t *)t.data();
                for(int i = 0; i < n; ++i) scores[i] = (float)p[i];
            }
            else if(t.dtype() == tensor::DType::UINT8)
            {
                uint8_t *p = (uint8_t *)t.data();
                for(int i = 0; i < n; ++i) scores[i] = (float)p[i];
            }
            else
            {
                throw err::Exception(err::ERR_NOT_IMPL, "unsupported output dtype for classifier");
            }
        }

        static void _softmax(std::vector<float> &scores)
        {
            if(scores.empty()) return;
            float max_val = *std::max_element(scores.begin(), scores.end());
            float sum = 0.0f;
            for(auto &v : scores)
            {
                v = std::exp(v - max_val);
                sum += v;
            }
            if(sum <= 0.0f) return;
            for(auto &v : scores) v /= sum;
        }

    private:
        image::Format _input_img_fmt;
        bool _dual_buff;
        bool _chw;
        nn_rk::NN *_model;
        std::map<std::string, std::string> _extra_info;
        image::Size _input_size;
        std::vector<nn_rk::LayerInfo> _inputs;
    };
}
