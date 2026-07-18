#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "maix_fs.hpp"
#include "maix_log.hpp"
#include "maix_tensor.hpp"
#include "z_image.hpp"
#include "z_nn_rk.hpp"

namespace maix::nn_rk
{
    class DetectObject
    {
    public:
        DetectObject(float x = 0, float y = 0, float w = 0, float h = 0, int class_id = 0, float score = 0)
            : x(x), y(y), w(w), h(h), class_id(class_id), score(score)
        {
        }

        std::string to_str() const
        {
            return "x: " + std::to_string(x) + ", y: " + std::to_string(y) + ", w: " + std::to_string(w) +
                   ", h: " + std::to_string(h) + ", class_id: " + std::to_string(class_id) +
                   ", score: " + std::to_string(score);
        }

        float x;
        float y;
        float w;
        float h;
        int class_id;
        float score;
    };

    class YOLOv5
    {
    public:
        YOLOv5(const std::string &model = "", bool dual_buff = true)
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

        ~YOLOv5()
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
            anchors.clear();
            label_path.clear();

            _model = new nn_rk::NN(model, _dual_buff);
            if(!_model)
            {
                return err::ERR_NO_MEM;
            }

            _extra_info = _model->extra_info();
            auto it_model_type = _extra_info.find("model_type");
            if(it_model_type == _extra_info.end())
            {
                log::error("model_type key not found");
                return err::ERR_ARGS;
            }
            const std::string &model_type = it_model_type->second;
            if(model_type != "yolov5" && model_type != "yolo")
            {
                log::error("model_type should be yolov5 or yolo, but got %s", model_type.c_str());
                return err::ERR_ARGS;
            }

            auto it_input_type = _extra_info.find("input_type");
            if(it_input_type != _extra_info.end())
            {
                std::string input_type = it_input_type->second;
                if(input_type == "rgb") _input_img_fmt = image::FMT_RGB888;
                else if(input_type == "bgr") _input_img_fmt = image::FMT_BGR888;
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
            if(_extra_info.find("anchors") != _extra_info.end())
            {
                if(_parse_float_list(_extra_info["anchors"], anchors) != err::ERR_NONE) return err::ERR_ARGS;
                if(anchors.size() < 6 || anchors.size() % 2 != 0)
                {
                    log::error("anchors should be even count and >= 6");
                    return err::ERR_ARGS;
                }
            }
            else
            {
                log::error("anchors key not found");
                return err::ERR_ARGS;
            }

            if(_extra_info.find("input_channel") != _extra_info.end() && _extra_info["input_channel"] == "hwc")
            {
                _chw = false;
            }

            err::Err e = _model->extra_info_labels(labels);
            if(e == err::ERR_NONE)
            {
                if(_extra_info.find("labels") != _extra_info.end())
                {
                    label_path = fs::dirname(_model->mud().model_path) + "/" + _extra_info["labels"];
                }
            }
            else
            {
                log::warn("labels not found in mud extra info");
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

        std::vector<DetectObject> *detect(image::Image &img, float conf_th = 0.25f, float iou_th = 0.45f, image::Fit fit = image::FIT_CONTAIN, int sort = 0)
        {
            _conf_th = conf_th;
            _iou_th = iou_th;

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
                return new std::vector<DetectObject>();
            }

            std::vector<DetectObject> *res = _post_process(outputs, img.width(), img.height(), fit, sort);
            delete outputs;
            return res;
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
        std::vector<float> anchors;

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

        static float _value(tensor::Tensor &t, int idx)
        {
            switch(t.dtype())
            {
                case tensor::DType::FLOAT32: return ((float *)t.data())[idx];
                case tensor::DType::FLOAT16: return _fp16_to_float(((uint16_t *)t.data())[idx]);
                case tensor::DType::INT8: return (float)((int8_t *)t.data())[idx];
                case tensor::DType::UINT8: return (float)((uint8_t *)t.data())[idx];
                case tensor::DType::INT16: return (float)((int16_t *)t.data())[idx];
                case tensor::DType::INT32: return (float)((int32_t *)t.data())[idx];
                default: return 0.0f;
            }
        }

        /**
         * Ultralytics ONNX/RKNN export: one 3D (or squeeze-2D) tensor [1, 4+nc, N] or [1, N, 4+nc].
         * For default Detect() with end2end==false, dist2bbox uses xywh=True: first 4 channels are
         * cx, cy, w, h in input-image pixels (not x1y1x2y2). Class channels are sigmoid probabilities.
         * Rockchip 3-head YOLO uses separate 4D outputs; this path returns nullptr for those.
         */
        std::vector<DetectObject> *_try_decode_ultralytics_concat(tensor::Tensor &t)
        {
            int class_num = (int)labels.size();
            if(class_num <= 0)
            {
                return nullptr;
            }
            const int box_dim = 4 + class_num;
            std::vector<int> shape = t.shape();
            int n_pred = 0;
            bool ch_major = false;

            if(shape.size() == 3)
            {
                if(shape[0] != 1)
                {
                    return nullptr;
                }
                if(shape[1] == box_dim)
                {
                    n_pred = shape[2];
                    ch_major = true;
                }
                else if(shape[2] == box_dim)
                {
                    n_pred = shape[1];
                    ch_major = false;
                }
                else
                {
                    return nullptr;
                }
            }
            else if(shape.size() == 2)
            {
                if(shape[0] == box_dim)
                {
                    n_pred = shape[1];
                    ch_major = true;
                }
                else if(shape[1] == box_dim)
                {
                    n_pred = shape[0];
                    ch_major = false;
                }
                else
                {
                    return nullptr;
                }
            }
            else
            {
                return nullptr;
            }

            if(n_pred <= 0)
            {
                return nullptr;
            }

            std::vector<DetectObject> *objs = new std::vector<DetectObject>();
            auto at = [&](int c, int j) -> float {
                int idx = ch_major ? (c * n_pred + j) : (j * box_dim + c);
                return _value(t, idx);
            };

            for(int j = 0; j < n_pred; ++j)
            {
                float cx = at(0, j);
                float cy = at(1, j);
                float bw = at(2, j);
                float bh = at(3, j);
                if(bw <= 0.0f || bh <= 0.0f)
                {
                    continue;
                }

                int best_cls = 0;
                float best_scr = at(4, j);
                for(int ci = 1; ci < class_num; ++ci)
                {
                    float s = at(4 + ci, j);
                    if(s > best_scr)
                    {
                        best_scr = s;
                        best_cls = ci;
                    }
                }
                if(best_scr <= _conf_th)
                {
                    continue;
                }

                float x0 = cx - bw * 0.5f;
                float y0 = cy - bh * 0.5f;
                objs->emplace_back(x0, y0, bw, bh, best_cls, best_scr);
            }
            return objs;
        }

        std::vector<DetectObject> *_post_process(tensor::Tensors *outputs, int img_w, int img_h, image::Fit fit, int sort)
        {
            if(outputs->size() == 1)
            {
                tensor::Tensor *one = outputs->begin()->second;
                std::vector<DetectObject> *from_ul = _try_decode_ultralytics_concat(*one);
                if(from_ul)
                {
                    std::vector<DetectObject> *objects = from_ul;
                    if(!objects->empty())
                    {
                        std::vector<DetectObject> *objects_total = objects;
                        objects = _nms(*objects_total);
                        delete objects_total;
                        if(sort != 0)
                        {
                            _sort_objects(*objects, sort);
                        }
                    }
                    if(!objects->empty())
                    {
                        _correct_bbox(*objects, img_w, img_h, fit);
                    }
                    return objects;
                }
            }

            std::vector<DetectObject> *objects = new std::vector<DetectObject>();
            int layer_num = outputs->size();
            if(layer_num <= 0)
            {
                return objects;
            }

            struct LayerTensor
            {
                tensor::Tensor *tensor;
                int h;
                int w;
            };
            std::vector<LayerTensor> layers;
            layers.reserve((size_t)layer_num);

            int class_num = (int)labels.size();
            int box_len = class_num + 5;
            for(auto it = outputs->begin(); it != outputs->end(); ++it)
            {
                tensor::Tensor *t = it->second;
                std::vector<int> shape = t->shape();
                if(shape.size() != 4)
                {
                    continue;
                }
                int h = 0;
                int w = 0;
                if(shape[1] % box_len == 0) // NCHW
                {
                    h = shape[2];
                    w = shape[3];
                }
                else if(shape[3] % box_len == 0) // NHWC
                {
                    h = shape[1];
                    w = shape[2];
                }
                else
                {
                    // Fallback: use the middle two dims.
                    h = shape[1];
                    w = shape[2];
                }
                layers.push_back({t, h, w});
            }

            if((int)layers.size() != layer_num)
            {
                log::warn("some output layers are not 4D, valid layers: %d/%d", (int)layers.size(), layer_num);
                layer_num = (int)layers.size();
                if(layer_num <= 0)
                {
                    return objects;
                }
            }

            // YOLOv5 anchors are ordered from small to large.
            // Match them to output layers by feature-map resolution: large map first (e.g. 80x80, 40x40, 20x20).
            std::sort(layers.begin(), layers.end(), [](const LayerTensor &a, const LayerTensor &b) {
                return (a.h * a.w) > (b.h * b.w);
            });

            for(int i = 0; i < layer_num; ++i)
            {
                _get_layer_objs(*objects, *layers[(size_t)i].tensor, i, layer_num);
            }

            if(!objects->empty())
            {
                std::vector<DetectObject> *objects_total = objects;
                objects = _nms(*objects_total);
                delete objects_total;
                if(sort != 0)
                {
                    _sort_objects(*objects, sort);
                }
            }
            if(!objects->empty())
            {
                _correct_bbox(*objects, img_w, img_h, fit);
            }
            return objects;
        }

        void _get_layer_objs(std::vector<DetectObject> &objs, tensor::Tensor &output, int layer_i, int layer_num)
        {
            std::vector<int> shape = output.shape();
            if(shape.size() != 4)
            {
                log::warn("unsupported output dim size: %d", (int)shape.size());
                return;
            }

            int class_num = (int)labels.size();
            if(class_num <= 0)
            {
                log::warn("labels empty, skip this layer");
                return;
            }

            int anchor_num = (int)anchors.size() / 2 / layer_num;
            if(anchor_num <= 0)
            {
                log::warn("anchor_num invalid");
                return;
            }

            int h = 0;
            int w = 0;
            bool nchw = false;
            int channels = 0;
            int box_len = class_num + 5;

            if(shape[1] == anchor_num * box_len)
            {
                nchw = true;
                channels = shape[1];
                h = shape[2];
                w = shape[3];
            }
            else if(shape[3] == anchor_num * box_len)
            {
                nchw = false;
                channels = shape[3];
                h = shape[1];
                w = shape[2];
            }
            else
            {
                log::warn("output shape not match anchors/labels: [%d,%d,%d,%d], anchor_num=%d, class_num=%d",
                          shape[0], shape[1], shape[2], shape[3], anchor_num, class_num);
                return;
            }

            int anchor_start = anchor_num * layer_i * 2;
            float scale_x = (float)_input_size.width() / w;
            float scale_y = (float)_input_size.height() / h;
            bool already_sigmoid = _tensor_seems_sigmoid(output);

            auto idx_of = [&](int c, int y, int x) -> int {
                if(nchw)
                {
                    return ((c * h) + y) * w + x;
                }
                return ((y * w) + x) * channels + c;
            };
            auto act = [&](float v) -> float {
                if(already_sigmoid)
                {
                    return v;
                }
                return _sigmoid(v);
            };

            for(int a = 0; a < anchor_num; ++a)
            {
                int base_c = a * box_len;
                for(int y = 0; y < h; ++y)
                {
                    for(int x = 0; x < w; ++x)
                    {
                        float obj_score = act(_value(output, idx_of(base_c + 4, y, x)));
                        if(obj_score <= _conf_th)
                            continue;

                        int best_cls = 0;
                        float best_cls_score = act(_value(output, idx_of(base_c + 5, y, x)));
                        for(int ci = 1; ci < class_num; ++ci)
                        {
                            float cls_score = act(_value(output, idx_of(base_c + 5 + ci, y, x)));
                            if(cls_score > best_cls_score)
                            {
                                best_cls_score = cls_score;
                                best_cls = ci;
                            }
                        }
                        float score = obj_score * best_cls_score;
                        if(score <= _conf_th)
                            continue;

                        float cx = (act(_value(output, idx_of(base_c + 0, y, x))) * 2.0f + x - 0.5f) * scale_x;
                        float cy = (act(_value(output, idx_of(base_c + 1, y, x))) * 2.0f + y - 0.5f) * scale_y;
                        float bw = std::pow(act(_value(output, idx_of(base_c + 2, y, x))) * 2.0f, 2.0f) * anchors[anchor_start + a * 2];
                        float bh = std::pow(act(_value(output, idx_of(base_c + 3, y, x))) * 2.0f, 2.0f) * anchors[anchor_start + a * 2 + 1];
                        float x0 = cx - bw * 0.5f;
                        float y0 = cy - bh * 0.5f;

                        objs.emplace_back(x0, y0, bw, bh, best_cls, score);
                    }
                }
            }
        }

        static bool _tensor_seems_sigmoid(tensor::Tensor &t)
        {
            int n = t.size_int();
            if(n <= 0)
            {
                return false;
            }
            int sample = std::min(n, 2048);
            float vmin = 1e9f;
            float vmax = -1e9f;
            for(int i = 0; i < sample; ++i)
            {
                float v = _value(t, i);
                if(v < vmin) vmin = v;
                if(v > vmax) vmax = v;
            }
            // Sigmoid output should be mostly in [0, 1].
            // Keep a tiny tolerance for numeric noise.
            return vmin >= -0.01f && vmax <= 1.01f;
        }

        std::vector<DetectObject> *_nms(std::vector<DetectObject> &objs)
        {
            std::vector<DetectObject> *result = new std::vector<DetectObject>();
            std::sort(objs.begin(), objs.end(), [](const DetectObject &a, const DetectObject &b) {
                return a.score > b.score;
            });

            for(size_t i = 0; i < objs.size(); ++i)
            {
                DetectObject &a = objs[i];
                if(a.score == 0.0f)
                {
                    continue;
                }
                for(size_t j = i + 1; j < objs.size(); ++j)
                {
                    DetectObject &b = objs[j];
                    if(b.score != 0.0f && a.class_id == b.class_id && _calc_iou(a, b) > _iou_th)
                    {
                        b.score = 0.0f;
                    }
                }
            }

            for(DetectObject &a : objs)
            {
                if(a.score <= 0.0f)
                {
                    continue;
                }
                if(a.x < 0)
                {
                    a.w += a.x;
                    a.x = 0;
                }
                if(a.y < 0)
                {
                    a.h += a.y;
                    a.y = 0;
                }
                if(a.x + a.w > _input_size.width())
                {
                    a.w = _input_size.width() - a.x;
                }
                if(a.y + a.h > _input_size.height())
                {
                    a.h = _input_size.height() - a.y;
                }
                if(a.w > 0 && a.h > 0)
                {
                    result->push_back(a);
                }
            }
            return result;
        }

        static void _sort_objects(std::vector<DetectObject> &objects, int sort)
        {
            if(sort > 0)
            {
                std::sort(objects.begin(), objects.end(), [](const DetectObject &a, const DetectObject &b) {
                    return (a.w * a.h) > (b.w * b.h);
                });
            }
            else
            {
                std::sort(objects.begin(), objects.end(), [](const DetectObject &a, const DetectObject &b) {
                    return (a.w * a.h) < (b.w * b.h);
                });
            }
        }

        void _correct_bbox(std::vector<DetectObject> &objs, int img_w, int img_h, image::Fit fit)
        {
#define RK_CORRECT_BBOX_RANGE(obj)    \
    do                                \
    {                                 \
        if((obj).x < 0)               \
        {                             \
            (obj).w += (obj).x;       \
            (obj).x = 0;              \
        }                             \
        if((obj).y < 0)               \
        {                             \
            (obj).h += (obj).y;       \
            (obj).y = 0;              \
        }                             \
        if((obj).x + (obj).w > img_w) \
        {                             \
            (obj).w = img_w - (obj).x; \
        }                             \
        if((obj).y + (obj).h > img_h) \
        {                             \
            (obj).h = img_h - (obj).y; \
        }                             \
    } while(0)

            if(img_w == _input_size.width() && img_h == _input_size.height())
            {
                return;
            }
            if(fit == image::FIT_FILL)
            {
                float scale_x = (float)img_w / _input_size.width();
                float scale_y = (float)img_h / _input_size.height();
                for(DetectObject &obj : objs)
                {
                    obj.x *= scale_x;
                    obj.y *= scale_y;
                    obj.w *= scale_x;
                    obj.h *= scale_y;
                    RK_CORRECT_BBOX_RANGE(obj);
                }
            }
            else if(fit == image::FIT_CONTAIN)
            {
                float scale_x = ((float)_input_size.width()) / img_w;
                float scale_y = ((float)_input_size.height()) / img_h;
                float scale = std::min(scale_x, scale_y);
                float scale_reverse = 1.0f / scale;
                float pad_w = (_input_size.width() - img_w * scale) / 2.0f;
                float pad_h = (_input_size.height() - img_h * scale) / 2.0f;
                for(DetectObject &obj : objs)
                {
                    obj.x = (obj.x - pad_w) * scale_reverse;
                    obj.y = (obj.y - pad_h) * scale_reverse;
                    obj.w *= scale_reverse;
                    obj.h *= scale_reverse;
                    RK_CORRECT_BBOX_RANGE(obj);
                }
            }
            else if(fit == image::FIT_COVER)
            {
                float scale_x = ((float)_input_size.width()) / img_w;
                float scale_y = ((float)_input_size.height()) / img_h;
                float scale = std::max(scale_x, scale_y);
                float scale_reverse = 1.0f / scale;
                float pad_w = (img_w * scale - _input_size.width()) / 2.0f;
                float pad_h = (img_h * scale - _input_size.height()) / 2.0f;
                for(DetectObject &obj : objs)
                {
                    obj.x = (obj.x + pad_w) * scale_reverse;
                    obj.y = (obj.y + pad_h) * scale_reverse;
                    obj.w *= scale_reverse;
                    obj.h *= scale_reverse;
                    RK_CORRECT_BBOX_RANGE(obj);
                }
            }
#undef RK_CORRECT_BBOX_RANGE
        }

        inline static float _sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

        inline static float _calc_iou(const DetectObject &a, const DetectObject &b)
        {
            float area1 = a.w * a.h;
            float area2 = b.w * b.h;
            float wi = std::min((a.x + a.w), (b.x + b.w)) - std::max(a.x, b.x);
            float hi = std::min((a.y + a.h), (b.y + b.h)) - std::max(a.y, b.y);
            float area_i = std::max(wi, 0.0f) * std::max(hi, 0.0f);
            return area_i / (area1 + area2 - area_i + 1e-6f);
        }

    private:
        image::Format _input_img_fmt;
        bool _dual_buff;
        bool _chw;
        nn_rk::NN *_model;
        std::map<std::string, std::string> _extra_info;
        image::Size _input_size;
        std::vector<nn_rk::LayerInfo> _inputs;
        float _conf_th = 0.25f;
        float _iou_th = 0.45f;
    };
}
