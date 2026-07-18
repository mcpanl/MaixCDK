#include "z_nn_rk.hpp"

#include <cstring>
#include <fstream>

#include "inifile.h"
#include "maix_fs.hpp"
#include "maix_log.hpp"
#include "rknn_api.h"

namespace maix::nn_rk
{
    struct RknnData
    {
        rknn_context ctx = 0;
        std::vector<unsigned char> model_data;
        std::vector<rknn_tensor_attr> input_attrs;
        std::vector<rknn_tensor_attr> output_attrs;
        std::vector<LayerInfo> inputs_info_cache;
        std::vector<LayerInfo> outputs_info_cache;
    };

    static void _get_section_keys(inifile::IniFile &ini, const char *section, std::vector<std::string> *keys)
    {
        inifile::IniSection *sect = ini.getSection(section);
        if(sect != nullptr)
        {
            for(inifile::IniSection::IniItem_it it = sect->begin(); it != sect->end(); ++it)
            {
                keys->push_back(it->key);
            }
        }
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
        ifs.read(reinterpret_cast<char *>(out.data()), (std::streamsize)size);
        return !ifs.fail();
    }

    static err::Err _load_labels_from_file(std::vector<std::string> &labels, const std::string &label_path)
    {
        labels.clear();
        fs::File *f = fs::open(label_path, "r");
        if(!f)
        {
            log::error("open label file %s failed", label_path.c_str());
            return err::ERR_ARGS;
        }
        std::string line;
        while(f->readline(line) > 0)
        {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if(!line.empty())
            {
                labels.push_back(line);
            }
        }
        f->close();
        delete f;
        return err::ERR_NONE;
    }

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

    static uint16_t _float_to_fp16(float f)
    {
        uint32_t x = 0;
        memcpy(&x, &f, sizeof(x));

        uint32_t sign = (x >> 16) & 0x8000u;
        uint32_t mantissa = x & 0x007fffffu;
        int exp = ((int)(x >> 23) & 0xff) - 127 + 15;

        if(exp <= 0)
        {
            if(exp < -10) return (uint16_t)sign;
            mantissa = (mantissa | 0x00800000u) >> (1 - exp);
            return (uint16_t)(sign | ((mantissa + 0x00001000u) >> 13));
        }
        if(exp >= 31)
        {
            return (uint16_t)(sign | 0x7c00u);
        }
        return (uint16_t)(sign | ((uint32_t)exp << 10) | ((mantissa + 0x00001000u) >> 13));
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

    MUD::MUD(const std::string &model_path)
    {
        this->model_path = model_path;
        if(!model_path.empty())
        {
            err::Err e = load(model_path);
            if(e != err::ERR_NONE)
            {
                throw err::Exception(e, "load mud failed");
            }
        }
    }

    MUD::~MUD()
    {
    }

    err::Err MUD::load(const std::string &model_path)
    {
        this->model_path = model_path;
        this->items.clear();
        this->type.clear();
        if(model_path.empty() || !fs::exists(model_path))
        {
            log::error("model path %s not exists", model_path.c_str());
            return err::ERR_ARGS;
        }

        if(model_path.rfind(".rknn") != std::string::npos)
        {
            this->type = "rknn";
            this->items["basic"]["type"] = "rknn";
            this->items["basic"]["model"] = fs::basename(model_path);
            return err::ERR_NONE;
        }

        if(model_path.rfind(".mud") == std::string::npos)
        {
            log::error("model path %s should be .mud or .rknn", model_path.c_str());
            return err::ERR_ARGS;
        }

        inifile::IniFile ini;
        int ret = ini.Load(model_path);
        if(ret != 0)
        {
            log::error("parse model %s failed, err %d", model_path.c_str(), ret);
            return err::ERR_ARGS;
        }

        if(ini.GetStringValue("basic", "type", &this->type) != 0)
        {
            log::error("parse model %s failed, not found type", model_path.c_str());
            return err::ERR_ARGS;
        }

        std::vector<std::string> sections;
        ret = ini.GetSections(&sections);
        if(ret <= 0)
        {
            log::error("parse model %s failed, get sections", model_path.c_str());
            return err::ERR_ARGS;
        }
        for(std::string &section : sections)
        {
            std::vector<std::string> keys;
            _get_section_keys(ini, section.c_str(), &keys);
            for(std::string &key : keys)
            {
                std::string value;
                if(ini.GetStringValue(section, key, &value) != 0)
                {
                    log::error("parse model %s failed, get key %s", model_path.c_str(), key.c_str());
                    return err::ERR_ARGS;
                }
                this->items[section][key] = value;
            }
        }
        return err::ERR_NONE;
    }

    err::Err MUD::parse_labels(std::vector<std::string> &labels, const std::string key)
    {
        auto it = items["extra"].find(key);
        if(it == items["extra"].end())
        {
            return err::ERR_NOT_FOUND;
        }
        const std::string &label_value = it->second;
        std::string label_file = fs::dirname(model_path) + "/" + label_value;
        if(fs::exists(label_file) && fs::isfile(label_file))
        {
            return _load_labels_from_file(labels, label_file);
        }

        labels.clear();
        size_t start = 0;
        size_t end = label_value.find(',');
        while(end != std::string::npos)
        {
            std::string label = label_value.substr(start, end - start);
            label.erase(0, label.find_first_not_of(" \t\r\n"));
            label.erase(label.find_last_not_of(" \t\r\n") + 1);
            if(!label.empty()) labels.push_back(label);
            start = end + 1;
            end = label_value.find(',', start);
        }
        std::string last = label_value.substr(start);
        last.erase(0, last.find_first_not_of(" \t\r\n"));
        last.erase(last.find_last_not_of(" \t\r\n") + 1);
        if(!last.empty()) labels.push_back(last);
        return err::ERR_NONE;
    }

    std::vector<std::string> MUD::parse_labels(const std::string key)
    {
        std::vector<std::string> labels;
        parse_labels(labels, key);
        return labels;
    }

    NN::NN(const std::string &model, bool dual_buff)
    {
        _loaded = false;
        _enable_dual_buff = dual_buff;
        _data = nullptr;
        if(!model.empty())
        {
            err::Err e = load(model);
            if(e != err::ERR_NONE)
            {
                throw err::Exception(e, "load rk model failed");
            }
        }
    }

    NN::~NN()
    {
        unload();
    }

    err::Err NN::load(const std::string &model)
    {
        if(_loaded)
        {
            unload();
        }
        if(model.empty())
        {
            return err::ERR_ARGS;
        }

        err::Err e = _mud.load(model);
        if(e != err::ERR_NONE)
        {
            return e;
        }
        std::string dir = fs::abspath(fs::dirname(_mud.model_path));
        std::string model_path = _resolve_rknn_path(_mud, dir);
        if(model_path.empty())
        {
            log::error("nn_rk: model path not found in mud/basic/model");
            return err::ERR_ARGS;
        }

        RknnData *d = new RknnData();
        if(!_read_file_bytes(model_path, d->model_data))
        {
            delete d;
            log::error("nn_rk: read model failed: %s", model_path.c_str());
            return err::ERR_IO;
        }

        int ret = rknn_init(&d->ctx, d->model_data.data(), (uint32_t)d->model_data.size(), 0, nullptr);
        if(ret != RKNN_SUCC)
        {
            delete d;
            log::error("nn_rk: rknn_init failed: %d", ret);
            return err::ERR_RUNTIME;
        }

        rknn_input_output_num io_num;
        memset(&io_num, 0, sizeof(io_num));
        ret = rknn_query(d->ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
        if(ret != RKNN_SUCC)
        {
            rknn_destroy(d->ctx);
            delete d;
            log::error("nn_rk: query io num failed: %d", ret);
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
                rknn_destroy(d->ctx);
                delete d;
                log::error("nn_rk: query input attr[%u] failed: %d", i, ret);
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
                rknn_destroy(d->ctx);
                delete d;
                log::error("nn_rk: query output attr[%u] failed: %d", i, ret);
                return err::ERR_RUNTIME;
            }
            d->output_attrs[i] = attr;
            d->outputs_info_cache.emplace_back(_name_from_attr(attr, "output_"), _rknn_to_maix_dtype(attr.type), _shape_from_attr(attr));
        }

        _data = d;
        _loaded = true;
        return err::ERR_NONE;
    }

    err::Err NN::unload()
    {
        if(!_loaded || !_data)
        {
            _loaded = false;
            return err::ERR_NONE;
        }
        RknnData *d = static_cast<RknnData *>(_data);
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

    bool NN::loaded()
    {
        return _loaded;
    }

    void NN::set_dual_buff(bool enable)
    {
        _enable_dual_buff = enable;
    }

    std::vector<LayerInfo> NN::inputs_info()
    {
        if(!_loaded || !_data)
        {
            return {};
        }
        return static_cast<RknnData *>(_data)->inputs_info_cache;
    }

    std::vector<LayerInfo> NN::outputs_info()
    {
        if(!_loaded || !_data)
        {
            return {};
        }
        return static_cast<RknnData *>(_data)->outputs_info_cache;
    }

    std::map<std::string, std::string> NN::extra_info()
    {
        return _mud.items["extra"];
    }

    std::vector<std::string> NN::extra_info_labels()
    {
        return _mud.parse_labels();
    }

    err::Err NN::extra_info_labels(std::vector<std::string> &labels)
    {
        return _mud.parse_labels(labels);
    }

    MUD &NN::mud()
    {
        return _mud;
    }

    err::Err NN::forward(tensor::Tensors &inputs, tensor::Tensors &outputs, bool copy_result, bool dual_buff_wait)
    {
        (void)_enable_dual_buff;
        (void)dual_buff_wait;
        (void)copy_result;

        if(!_loaded || !_data)
        {
            log::error("nn_rk: model not loaded");
            return err::ERR_NOT_PERMIT;
        }

        RknnData *d = static_cast<RknnData *>(_data);
        if(inputs.size() != d->input_attrs.size())
        {
            log::error("nn_rk: input count mismatch: got %d expect %d", (int)inputs.size(), (int)d->input_attrs.size());
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
                log::error("nn_rk: unsupported input dtype: %d", (int)src->dtype());
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
            log::error("nn_rk: rknn_inputs_set failed: %d", ret);
            return err::ERR_RUNTIME;
        }

        ret = rknn_run(d->ctx, nullptr);
        if(ret != RKNN_SUCC)
        {
            log::error("nn_rk: rknn_run failed: %d", ret);
            return err::ERR_RUNTIME;
        }

        std::vector<rknn_output> rk_outputs(d->output_attrs.size());
        memset(rk_outputs.data(), 0, rk_outputs.size() * sizeof(rknn_output));
        for(size_t i = 0; i < rk_outputs.size(); ++i)
        {
            // Ask RKNN to return dequantized float outputs.
            // This avoids handling int8/uint8 scale/zp manually in each post-process.
            rk_outputs[i].want_float = 1;
            rk_outputs[i].is_prealloc = 0;
        }

        ret = rknn_outputs_get(d->ctx, (uint32_t)rk_outputs.size(), rk_outputs.data(), nullptr);
        if(ret != RKNN_SUCC)
        {
            log::error("nn_rk: rknn_outputs_get failed: %d", ret);
            return err::ERR_RUNTIME;
        }

        outputs.clear();
        for(size_t i = 0; i < d->output_attrs.size(); ++i)
        {
            const rknn_tensor_attr &attr = d->output_attrs[i];
            std::vector<int> shape = _shape_from_attr(attr);
            // want_float=1 means rk_outputs[i].buf stores float32 data.
            tensor::DType dtype = tensor::DType::FLOAT32;
            std::string name = _name_from_attr(attr, "output_");
            tensor::Tensor *out = new tensor::Tensor(shape, dtype, rk_outputs[i].buf, true);
            outputs.add_tensor(name, out, false, true);
        }
        rknn_outputs_release(d->ctx, (uint32_t)rk_outputs.size(), rk_outputs.data());
        return err::ERR_NONE;
    }

    tensor::Tensors *NN::forward(tensor::Tensors &inputs, bool copy_result, bool dual_buff_wait)
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

    tensor::Tensors *NN::forward_image(image::Image &img, std::vector<float> mean, std::vector<float> scale, image::Fit fit, bool copy_result, bool dual_buff_wait, bool chw)
    {
        (void)chw;

        if(!_loaded || !_data)
        {
            log::error("nn_rk: model not loaded");
            return nullptr;
        }
        RknnData *d = static_cast<RknnData *>(_data);
        if(d->input_attrs.empty())
        {
            return nullptr;
        }

        const rknn_tensor_attr &in_attr = d->input_attrs[0];
        std::vector<int> in_shape = _shape_from_attr(in_attr);
        if(in_shape.size() < 3)
        {
            log::error("nn_rk: unsupported input dims: %d", (int)in_shape.size());
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

        image::Image *converted = nullptr;
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
        uint8_t *pix = (uint8_t *)input_img->data();
        if(!pix)
        {
            if(converted) delete converted;
            if(need_delete_resized) delete resized;
            return nullptr;
        }

        int model_c = 3;
        if(in_shape.size() == 4)
        {
            model_c = model_chw ? in_shape[1] : in_shape[3];
        }
        else if(in_shape.size() == 3)
        {
            model_c = model_chw ? in_shape[0] : in_shape[2];
        }
        if(model_c <= 0) model_c = 3;

        std::vector<float> fp_data((size_t)model_h * model_w * model_c);
        bool do_norm = (!mean.empty() && !scale.empty());
        auto get_mean = [&](int c) -> float {
            if(!do_norm) return 0.0f;
            if((int)mean.size() == 1) return mean[0];
            if(c < (int)mean.size()) return mean[c];
            return mean.back();
        };
        auto get_scale = [&](int c) -> float {
            if(!do_norm) return 1.0f;
            if((int)scale.size() == 1) return scale[0];
            if(c < (int)scale.size()) return scale[c];
            return scale.back();
        };

        // input_img is RGB888, layout converted based on model fmt.
        if(model_chw)
        {
            for(int c = 0; c < model_c; ++c)
            {
                float m = get_mean(c);
                float s = get_scale(c);
                for(int h = 0; h < model_h; ++h)
                {
                    for(int w = 0; w < model_w; ++w)
                    {
                        int src_idx = (h * model_w + w) * 3 + c;
                        float v = (float)pix[src_idx];
                        fp_data[(size_t)c * model_h * model_w + (size_t)h * model_w + w] = (v - m) * s;
                    }
                }
            }
        }
        else
        {
            for(int h = 0; h < model_h; ++h)
            {
                for(int w = 0; w < model_w; ++w)
                {
                    for(int c = 0; c < model_c; ++c)
                    {
                        int src_idx = (h * model_w + w) * 3 + c;
                        float v = (float)pix[src_idx];
                        fp_data[((size_t)h * model_w + w) * model_c + c] = (v - get_mean(c)) * get_scale(c);
                    }
                }
            }
        }

        tensor::Tensor *input_tensor = nullptr;
        if(in_attr.type == RKNN_TENSOR_FLOAT32)
        {
            input_tensor = new tensor::Tensor(in_shape, tensor::DType::FLOAT32, fp_data.data(), true);
        }
        else if(in_attr.type == RKNN_TENSOR_FLOAT16)
        {
            std::vector<uint16_t> fp16_data(fp_data.size());
            for(size_t i = 0; i < fp_data.size(); ++i)
            {
                fp16_data[i] = _float_to_fp16(fp_data[i]);
            }
            input_tensor = new tensor::Tensor(in_shape, tensor::DType::FLOAT16, fp16_data.data(), true);
        }
        else if(in_attr.type == RKNN_TENSOR_INT8)
        {
            std::vector<int8_t> i8_data(fp_data.size());
            for(size_t i = 0; i < fp_data.size(); ++i)
            {
                int v = (int)std::lround(fp_data[i]);
                if(v < -128) v = -128;
                if(v > 127) v = 127;
                i8_data[i] = (int8_t)v;
            }
            input_tensor = new tensor::Tensor(in_shape, tensor::DType::INT8, i8_data.data(), true);
        }
        else
        {
            std::vector<uint8_t> u8_data(fp_data.size());
            for(size_t i = 0; i < fp_data.size(); ++i)
            {
                int v = (int)std::lround(fp_data[i]);
                if(v < 0) v = 0;
                if(v > 255) v = 255;
                u8_data[i] = (uint8_t)v;
            }
            input_tensor = new tensor::Tensor(in_shape, tensor::DType::UINT8, u8_data.data(), true);
        }
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
