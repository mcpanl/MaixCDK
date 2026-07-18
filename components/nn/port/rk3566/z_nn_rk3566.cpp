#include "z_nn_rk3566.hpp"

#include <fstream>
#include <cstring>
#include <vector>

#include "maix_log.hpp"
#include "rknn_api.h"

namespace maix::nn
{
    struct RknnNNData
    {
        rknn_context ctx = 0;
        std::vector<unsigned char> model_data;
        std::vector<rknn_tensor_attr> input_attrs;
        std::vector<rknn_tensor_attr> output_attrs;
        std::vector<LayerInfo> inputs_info_cache;
        std::vector<LayerInfo> outputs_info_cache;
    };

    static tensor::DType _rknn_to_maix_dtype(rknn_tensor_type type)
    {
        switch(type)
        {
            case RKNN_TENSOR_FLOAT32: return tensor::DType::FLOAT32;
            case RKNN_TENSOR_FLOAT16: return tensor::DType::FLOAT16;
            case RKNN_TENSOR_INT8: return tensor::DType::INT8;
            case RKNN_TENSOR_UINT8: return tensor::DType::UINT8;
            case RKNN_TENSOR_INT16: return tensor::DType::INT16;
#ifdef RKNN_TENSOR_UINT16
            case RKNN_TENSOR_UINT16: return tensor::DType::UINT16;
#endif
            case RKNN_TENSOR_INT32: return tensor::DType::INT32;
#ifdef RKNN_TENSOR_UINT32
            case RKNN_TENSOR_UINT32: return tensor::DType::UINT32;
#endif
            default: return tensor::DType::FLOAT32;
        }
    }

    static bool _maix_to_rknn_dtype(tensor::DType dtype, rknn_tensor_type &type)
    {
        switch(dtype)
        {
            case tensor::DType::FLOAT32: type = RKNN_TENSOR_FLOAT32; return true;
            case tensor::DType::FLOAT16: type = RKNN_TENSOR_FLOAT16; return true;
            case tensor::DType::INT8: type = RKNN_TENSOR_INT8; return true;
            case tensor::DType::UINT8: type = RKNN_TENSOR_UINT8; return true;
            case tensor::DType::INT16: type = RKNN_TENSOR_INT16; return true;
#ifdef RKNN_TENSOR_UINT16
            case tensor::DType::UINT16: type = RKNN_TENSOR_UINT16; return true;
#endif
            case tensor::DType::INT32: type = RKNN_TENSOR_INT32; return true;
#ifdef RKNN_TENSOR_UINT32
            case tensor::DType::UINT32: type = RKNN_TENSOR_UINT32; return true;
#endif
            default: return false;
        }
    }

    static std::vector<int> _shape_from_attr(const rknn_tensor_attr &attr)
    {
        std::vector<int> shape;
        for(uint32_t i = 0; i < attr.n_dims; ++i)
        {
            shape.push_back((int)attr.dims[i]);
        }
        if(shape.empty())
        {
            shape.push_back(1);
        }
        return shape;
    }

    static std::string _name_from_attr(const rknn_tensor_attr &attr, const char *prefix)
    {
        if(attr.name[0] != '\0')
        {
            return std::string(attr.name);
        }
        return std::string(prefix) + std::to_string(attr.index);
    }

    static bool _read_file_bytes(const std::string &path, std::vector<unsigned char> &out)
    {
        std::ifstream ifs(path, std::ios::binary);
        if(!ifs)
        {
            return false;
        }
        ifs.seekg(0, std::ios::end);
        std::streampos size = ifs.tellg();
        if(size <= 0)
        {
            return false;
        }
        out.resize((size_t)size);
        ifs.seekg(0, std::ios::beg);
        ifs.read(reinterpret_cast<char*>(out.data()), (std::streamsize)size);
        return !ifs.fail();
    }

    static std::string _resolve_rknn_path(const MUD &mud, const std::string &dir)
    {
        if(mud.model_path.rfind(".rknn") != std::string::npos)
        {
            return mud.model_path;
        }
        auto sec_it = mud.items.find("basic");
        if(sec_it == mud.items.end())
        {
            return "";
        }
        auto model_it = sec_it->second.find("model");
        if(model_it == sec_it->second.end())
        {
            return "";
        }
        return dir + "/" + model_it->second;
    }

    NN_RK3566::NN_RK3566(bool dual_buff)
    {
        _loaded = false;
        _enable_dual_buff = dual_buff;
        _data = nullptr;
    }

    NN_RK3566::~NN_RK3566()
    {
        unload();
    }

    err::Err NN_RK3566::load(const MUD &mud, const std::string &dir)
    {
        if(_loaded)
        {
            unload();
        }

        std::string model_path = _resolve_rknn_path(mud, dir);
        if(model_path.empty())
        {
            log::error("NN_RK3566: model path not found, use .rknn or basic/model in mud");
            return err::ERR_ARGS;
        }

        RknnNNData *d = new RknnNNData();
        if(!_read_file_bytes(model_path, d->model_data))
        {
            log::error("NN_RK3566: read model failed: %s", model_path.c_str());
            delete d;
            return err::ERR_IO;
        }

        int ret = rknn_init(&d->ctx, d->model_data.data(), (uint32_t)d->model_data.size(), 0, nullptr);
        if(ret != RKNN_SUCC)
        {
            log::error("NN_RK3566: rknn_init failed: %d", ret);
            delete d;
            return err::ERR_RUNTIME;
        }

        rknn_input_output_num io_num;
        memset(&io_num, 0, sizeof(io_num));
        ret = rknn_query(d->ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if(ret != RKNN_SUCC)
        {
            log::error("NN_RK3566: query in/out num failed: %d", ret);
            rknn_destroy(d->ctx);
            delete d;
            return err::ERR_RUNTIME;
        }

        d->input_attrs.resize(io_num.n_input);
        d->inputs_info_cache.reserve(io_num.n_input);
        for(uint32_t i = 0; i < io_num.n_input; ++i)
        {
            rknn_tensor_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.index = i;
            ret = rknn_query(d->ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
            if(ret != RKNN_SUCC)
            {
                log::error("NN_RK3566: query input attr[%u] failed: %d", i, ret);
                rknn_destroy(d->ctx);
                delete d;
                return err::ERR_RUNTIME;
            }
            d->input_attrs[i] = attr;
            d->inputs_info_cache.emplace_back(_name_from_attr(attr, "input_"), _rknn_to_maix_dtype(attr.type), _shape_from_attr(attr));
        }

        d->output_attrs.resize(io_num.n_output);
        d->outputs_info_cache.reserve(io_num.n_output);
        for(uint32_t i = 0; i < io_num.n_output; ++i)
        {
            rknn_tensor_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.index = i;
            ret = rknn_query(d->ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
            if(ret != RKNN_SUCC)
            {
                log::error("NN_RK3566: query output attr[%u] failed: %d", i, ret);
                rknn_destroy(d->ctx);
                delete d;
                return err::ERR_RUNTIME;
            }
            d->output_attrs[i] = attr;
            d->outputs_info_cache.emplace_back(_name_from_attr(attr, "output_"), _rknn_to_maix_dtype(attr.type), _shape_from_attr(attr));
        }

        _data = d;
        _loaded = true;
        return err::ERR_NONE;
    }

    err::Err NN_RK3566::unload()
    {
        if(!_loaded || !_data)
        {
            _loaded = false;
            return err::ERR_NONE;
        }
        RknnNNData *d = static_cast<RknnNNData*>(_data);
        if(d->ctx)
        {
            rknn_destroy(d->ctx);
            d->ctx = 0;
        }
        delete d;
        _data = nullptr;
        _loaded = false;
        return err::ERR_NONE;
    }

    bool NN_RK3566::loaded()
    {
        return _loaded;
    }

    void NN_RK3566::set_dual_buff(bool enable)
    {
        _enable_dual_buff = enable;
    }

    std::vector<LayerInfo> NN_RK3566::inputs_info()
    {
        if(!_loaded || !_data)
        {
            return {};
        }
        return static_cast<RknnNNData*>(_data)->inputs_info_cache;
    }

    std::vector<LayerInfo> NN_RK3566::outputs_info()
    {
        if(!_loaded || !_data)
        {
            return {};
        }
        return static_cast<RknnNNData*>(_data)->outputs_info_cache;
    }

    err::Err NN_RK3566::forward(tensor::Tensors &inputs, tensor::Tensors &outputs, bool copy_result, bool dual_buff_wait)
    {
        (void)_enable_dual_buff;
        (void)dual_buff_wait;
        (void)copy_result;

        if(!_loaded || !_data)
        {
            log::error("NN_RK3566: model not loaded");
            return err::ERR_NOT_PERMIT;
        }

        RknnNNData *d = static_cast<RknnNNData*>(_data);
        if(inputs.size() != d->input_attrs.size())
        {
            log::error("NN_RK3566: input tensor count mismatch, got %d expect %d",
                       (int)inputs.size(), (int)d->input_attrs.size());
            return err::ERR_ARGS;
        }

        std::vector<std::string> input_keys = inputs.keys();
        std::vector<rknn_input> rk_inputs(d->input_attrs.size());
        memset(rk_inputs.data(), 0, rk_inputs.size() * sizeof(rknn_input));
        for(size_t i = 0; i < d->input_attrs.size(); ++i)
        {
            const rknn_tensor_attr &attr = d->input_attrs[i];
            tensor::Tensor *src = nullptr;

            auto by_name = inputs.tensors.find(_name_from_attr(attr, "input_"));
            if(by_name != inputs.tensors.end())
            {
                src = by_name->second;
            }
            else
            {
                src = inputs.tensors[input_keys[i]];
            }

            rknn_tensor_type in_type;
            if(!_maix_to_rknn_dtype(src->dtype(), in_type))
            {
                log::error("NN_RK3566: unsupported input dtype for %s", _name_from_attr(attr, "input_").c_str());
                return err::ERR_ARGS;
            }

            rk_inputs[i].index = i;
            rk_inputs[i].buf = src->data();
            rk_inputs[i].size = (uint32_t)(src->size_int() * tensor::dtype_size[src->dtype()]);
            rk_inputs[i].type = in_type;
            rk_inputs[i].fmt = attr.fmt;
            rk_inputs[i].pass_through = 0;
        }

        int ret = rknn_inputs_set(d->ctx, (uint32_t)rk_inputs.size(), rk_inputs.data());
        if(ret != RKNN_SUCC)
        {
            log::error("NN_RK3566: rknn_inputs_set failed: %d", ret);
            return err::ERR_RUNTIME;
        }

        ret = rknn_run(d->ctx, nullptr);
        if(ret != RKNN_SUCC)
        {
            log::error("NN_RK3566: rknn_run failed: %d", ret);
            return err::ERR_RUNTIME;
        }

        std::vector<rknn_output> rk_outputs(d->output_attrs.size());
        memset(rk_outputs.data(), 0, rk_outputs.size() * sizeof(rknn_output));
        for(size_t i = 0; i < rk_outputs.size(); ++i)
        {
            rk_outputs[i].want_float = 0;
            rk_outputs[i].is_prealloc = 0;
        }

        ret = rknn_outputs_get(d->ctx, (uint32_t)rk_outputs.size(), rk_outputs.data(), nullptr);
        if(ret != RKNN_SUCC)
        {
            log::error("NN_RK3566: rknn_outputs_get failed: %d", ret);
            return err::ERR_RUNTIME;
        }

        outputs.clear();
        for(size_t i = 0; i < d->output_attrs.size(); ++i)
        {
            const rknn_tensor_attr &attr = d->output_attrs[i];
            std::vector<int> shape = _shape_from_attr(attr);
            tensor::DType dtype = _rknn_to_maix_dtype(attr.type);
            std::string name = _name_from_attr(attr, "output_");
            tensor::Tensor *out = new tensor::Tensor(shape, dtype, rk_outputs[i].buf, true);
            outputs.add_tensor(name, out, false, true);
        }

        rknn_outputs_release(d->ctx, (uint32_t)rk_outputs.size(), rk_outputs.data());
        return err::ERR_NONE;
    }

    tensor::Tensors *NN_RK3566::forward(tensor::Tensors &inputs, bool copy_result, bool dual_buff_wait)
    {
        tensor::Tensors *outputs = new tensor::Tensors();
        err::Err e = forward(inputs, *outputs, copy_result, dual_buff_wait);
        if(e != err::ERR_NONE)
        {
            delete outputs;
            return nullptr;
        }
        return outputs;
    }

    tensor::Tensors *NN_RK3566::forward_image(image::Image &img, std::vector<float> mean, std::vector<float> scale, image::Fit fit, bool copy_result, bool dual_buff_wait, bool chw)
    {
        (void)mean;
        (void)scale;

        if(!_loaded || !_data)
        {
            log::error("NN_RK3566: model not loaded");
            return nullptr;
        }

        RknnNNData *d = static_cast<RknnNNData*>(_data);
        if(d->input_attrs.empty())
        {
            log::error("NN_RK3566: model has no input");
            return nullptr;
        }

        const rknn_tensor_attr &in_attr = d->input_attrs[0];
        std::vector<int> in_shape = _shape_from_attr(in_attr);
        if(in_shape.size() < 3)
        {
            log::error("NN_RK3566: unsupported input dims: %d", (int)in_shape.size());
            return nullptr;
        }

        bool model_chw = (in_attr.fmt == RKNN_TENSOR_NCHW);
        int model_h = 0;
        int model_w = 0;
        if(in_shape.size() == 4)
        {
            if(model_chw)
            {
                model_h = in_shape[2];
                model_w = in_shape[3];
            }
            else
            {
                model_h = in_shape[1];
                model_w = in_shape[2];
            }
        }
        else
        {
            if(model_chw)
            {
                model_h = in_shape[1];
                model_w = in_shape[2];
            }
            else
            {
                model_h = in_shape[0];
                model_w = in_shape[1];
            }
        }

        image::Image *resized = &img;
        image::Image *converted = nullptr;
        bool need_delete_resized = false;
        if(img.width() != model_w || img.height() != model_h)
        {
            resized = img.resize(model_w, model_h, fit);
            if(!resized)
            {
                return nullptr;
            }
            need_delete_resized = true;
        }
        if(resized->format() != image::FMT_RGB888)
        {
            converted = resized->to_format(image::FMT_RGB888);
            if(!converted)
            {
                if(need_delete_resized) delete resized;
                return nullptr;
            }
        }

        image::Image *input_img = converted ? converted : resized;
        tensor::Tensor *input_tensor = input_img->to_tensor(model_chw, true);
        if(!input_tensor)
        {
            if(converted) delete converted;
            if(need_delete_resized) delete resized;
            return nullptr;
        }

        tensor::Tensors input_tensors;
        input_tensors.add_tensor(_name_from_attr(in_attr, "input_"), input_tensor, false, true);
        tensor::Tensors *outputs = forward(input_tensors, copy_result, dual_buff_wait);

        if(converted) delete converted;
        if(need_delete_resized) delete resized;
        return outputs;
    }
}
