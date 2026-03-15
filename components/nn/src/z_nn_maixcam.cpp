//
// Created by satuo on 2025/11/16.
//
#include "z_nn_maixcam.hpp"
#include "maix_log.hpp"
#include <cviruntime.h>
#include <string>
#include <vector>
#include <cstring>

using namespace maix;

namespace maix::nn {

    // ──────────────────────────────────────────────────────────────────────────
    // Internal per-instance state stored via _data void*
    // ──────────────────────────────────────────────────────────────────────────
    struct MaixCamNNData {
        CVI_MODEL_HANDLE model = nullptr;

        // tensor arrays obtained from CVI_NN_GetInputOutputTensors
        CVI_TENSOR *input_tensors  = nullptr;
        int32_t     input_num      = 0;
        CVI_TENSOR *output_tensors = nullptr;
        int32_t     output_num     = 0;

        // cached LayerInfo lists (built once at load time)
        std::vector<LayerInfo> inputs_info_cache;
        std::vector<LayerInfo> outputs_info_cache;
    };

    // ──────────────────────────────────────────────────────────────────────────
    // Helper: map CVI_FMT -> maix::tensor::DType
    // ──────────────────────────────────────────────────────────────────────────
    static tensor::DType _cvi_fmt_to_dtype(CVI_FMT fmt)
    {
        switch (fmt) {
            case CVI_FMT_FP32:   return tensor::DType::FLOAT32;
            case CVI_FMT_INT32:  return tensor::DType::INT32;
            case CVI_FMT_UINT32: return tensor::DType::UINT32;
            case CVI_FMT_INT16:  return tensor::DType::INT16;
            case CVI_FMT_UINT16: return tensor::DType::UINT16;
            case CVI_FMT_INT8:   return tensor::DType::INT8;
            case CVI_FMT_UINT8:  return tensor::DType::UINT8;
            case CVI_FMT_BF16:   return tensor::DType::FLOAT16; // closest available
            default:             return tensor::DType::FLOAT32;
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // Helper: build LayerInfo list from a CVI_TENSOR array
    // ──────────────────────────────────────────────────────────────────────────
    static std::vector<LayerInfo> _build_layer_info(CVI_TENSOR *tensors, int32_t num)
    {
        std::vector<LayerInfo> info;
        info.reserve(num);
        for (int32_t i = 0; i < num; i++) {
            CVI_TENSOR &t = tensors[i];
            std::string name = t.name ? t.name : ("tensor_" + std::to_string(i));
            tensor::DType dtype = _cvi_fmt_to_dtype(t.fmt);
            std::vector<int> shape;
            shape.reserve(t.shape.dim_size);
            for (size_t d = 0; d < t.shape.dim_size; d++) {
                shape.push_back(t.shape.dim[d]);
            }
            info.emplace_back(name, dtype, shape);
        }
        return info;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // maixcam_load_cvimodel: load .cvimodel and fill MUD metadata
    // ──────────────────────────────────────────────────────────────────────────
    err::Err maixcam_load_cvimodel(const std::string &model_path, MUD *mud_obj)
    {
        log::info("maixcam_load_cvimodel: %s", model_path.c_str());

        CVI_MODEL_HANDLE model = nullptr;
        CVI_RC rc = CVI_NN_RegisterModel(model_path.c_str(), &model);
        if (rc != CVI_RC_SUCCESS) {
            log::error("CVI_NN_RegisterModel failed: %d", rc);
            return err::ERR_RUNTIME;
        }

        CVI_TENSOR *inputs  = nullptr; int32_t input_num  = 0;
        CVI_TENSOR *outputs = nullptr; int32_t output_num = 0;
        rc = CVI_NN_GetInputOutputTensors(model, &inputs, &input_num, &outputs, &output_num);
        if (rc != CVI_RC_SUCCESS) {
            log::error("CVI_NN_GetInputOutputTensors failed: %d", rc);
            CVI_NN_CleanupModel(model);
            return err::ERR_RUNTIME;
        }

        // Fill MUD type (generic cvimodel)
        mud_obj->type = "cvimodel";
        mud_obj->model_path = model_path;

        // Fill basic section: input / output shapes so callers can inspect
        auto fill_section = [&](const char *prefix, CVI_TENSOR *t, int32_t num) {
            for (int32_t i = 0; i < num; i++) {
                std::string key = std::string(prefix) + std::to_string(i);
                std::string val;
                for (size_t d = 0; d < t[i].shape.dim_size; d++) {
                    if (d) val += "x";
                    val += std::to_string(t[i].shape.dim[d]);
                }
                mud_obj->items["extra"][key] = val;
                if (t[i].name)
                    mud_obj->items["extra"][key + "_name"] = std::string(t[i].name);
            }
        };
        fill_section("input_",  inputs,  input_num);
        fill_section("output_", outputs, output_num);

        CVI_NN_CleanupModel(model);
        return err::ERR_NONE;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // NN_MaixCam implementation
    // ──────────────────────────────────────────────────────────────────────────
    NN_MaixCam::NN_MaixCam(bool dual_buff)
    {
        _init(dual_buff);
    }

    NN_MaixCam::NN_MaixCam()
    {
        _init(false);
    }

    NN_MaixCam::~NN_MaixCam()
    {
        unload();
    }

    void NN_MaixCam::_init(bool dual_buff)
    {
        _loaded           = false;
        _enable_dual_buff = dual_buff;
        _data             = nullptr;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // load
    // ──────────────────────────────────────────────────────────────────────────
    err::Err NN_MaixCam::load(const MUD &mud, const std::string &dir)
    {
        if (_loaded) {
            log::warn("model already loaded, unload first");
            unload();
        }

        // Resolve the actual .cvimodel path
        // MUD items["basic"]["model"] holds the relative path for .mud files.
        // For a direct .cvimodel the model_path in MUD IS the file.
        std::string cvi_path;
        if (mud.model_path.find(".cvimodel") != std::string::npos) {
            cvi_path = mud.model_path;
        } else {
            auto it_basic = mud.items.find("basic");
            if (it_basic != mud.items.end()) {
                auto it_model = it_basic->second.find("model");
                if (it_model != it_basic->second.end()) {
                    cvi_path = dir + "/" + it_model->second;
                }
            }
            if (cvi_path.empty()) {
                log::error("Cannot resolve cvimodel path from MUD");
                return err::ERR_ARGS;
            }
        }

        log::info("NN_MaixCam::load %s", cvi_path.c_str());

        MaixCamNNData *d = new MaixCamNNData();

        CVI_RC rc = CVI_NN_RegisterModel(cvi_path.c_str(), &d->model);
        if (rc != CVI_RC_SUCCESS) {
            log::error("CVI_NN_RegisterModel failed: %d", rc);
            delete d;
            return err::ERR_RUNTIME;
        }

        rc = CVI_NN_GetInputOutputTensors(d->model,
                                          &d->input_tensors,  &d->input_num,
                                          &d->output_tensors, &d->output_num);
        if (rc != CVI_RC_SUCCESS) {
            log::error("CVI_NN_GetInputOutputTensors failed: %d", rc);
            CVI_NN_CleanupModel(d->model);
            delete d;
            return err::ERR_RUNTIME;
        }

        d->inputs_info_cache  = _build_layer_info(d->input_tensors,  d->input_num);
        d->outputs_info_cache = _build_layer_info(d->output_tensors, d->output_num);

        _data   = d;
        _loaded = true;
        log::info("NN_MaixCam: loaded %d inputs, %d outputs", d->input_num, d->output_num);
        return err::ERR_NONE;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // unload
    // ──────────────────────────────────────────────────────────────────────────
    err::Err NN_MaixCam::unload()
    {
        if (!_loaded || !_data) {
            _loaded = false;
            return err::ERR_NONE;
        }
        MaixCamNNData *d = static_cast<MaixCamNNData *>(_data);
        if (d->model) {
            CVI_NN_CleanupModel(d->model);
            d->model = nullptr;
        }
        delete d;
        _data   = nullptr;
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
        if (!_loaded || !_data) return {};
        return static_cast<MaixCamNNData *>(_data)->inputs_info_cache;
    }

    std::vector<LayerInfo> NN_MaixCam::outputs_info()
    {
        if (!_loaded || !_data) return {};
        return static_cast<MaixCamNNData *>(_data)->outputs_info_cache;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // forward (in-place output reference version)
    // ──────────────────────────────────────────────────────────────────────────
    err::Err NN_MaixCam::forward(tensor::Tensors &inputs,
                                 tensor::Tensors &outputs,
                                 bool copy_result,
                                 bool dual_buff_wait)
    {
        if (!_loaded || !_data) {
            log::error("model not loaded");
            return err::ERR_NOT_PERMIT;
        }
        MaixCamNNData *d = static_cast<MaixCamNNData *>(_data);

        // ── copy caller's input tensors into cviruntime buffers ──────────────
        auto in_keys = inputs.keys();
        if ((int)in_keys.size() != d->input_num) {
            log::error("input tensor count mismatch: got %d, expect %d",
                       (int)in_keys.size(), d->input_num);
            return err::ERR_ARGS;
        }
        for (int i = 0; i < d->input_num; i++) {
            CVI_TENSOR &ct = d->input_tensors[i];
            void *dst = CVI_NN_TensorPtr(&ct);
            if (!dst) {
                log::error("CVI_NN_TensorPtr returned null for input %d", i);
                return err::ERR_RUNTIME;
            }
            tensor::Tensor &src = inputs[in_keys[i]];
            size_t bytes = CVI_NN_TensorSize(&ct);
            memcpy(dst, src.data(), bytes);
        }

        // ── inference ────────────────────────────────────────────────────────
        CVI_RC rc = CVI_NN_Forward(d->model,
                                   d->input_tensors,  d->input_num,
                                   d->output_tensors, d->output_num);
        if (rc != CVI_RC_SUCCESS) {
            log::error("CVI_NN_Forward failed: %d", rc);
            return err::ERR_RUNTIME;
        }

        // ── collect outputs ──────────────────────────────────────────────────
        for (int i = 0; i < d->output_num; i++) {
            CVI_TENSOR &ct = d->output_tensors[i];
            const LayerInfo &li = d->outputs_info_cache[i];
            void *src = CVI_NN_TensorPtr(&ct);

            auto it = outputs.tensors.find(li.name);
            if (it != outputs.tensors.end()) {
                // caller pre-allocated – just copy into it
                memcpy(it->second->data(), src, CVI_NN_TensorSize(&ct));
            } else {
                // allocate a new tensor
                tensor::Tensor *t = new tensor::Tensor(li.shape, li.dtype, src, copy_result);
                outputs.add_tensor(li.name, t, false, true);
            }
        }
        return err::ERR_NONE;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // forward (returns new Tensors*, MaixPy-friendly)
    // ──────────────────────────────────────────────────────────────────────────
    tensor::Tensors *NN_MaixCam::forward(tensor::Tensors &inputs,
                                         bool copy_result,
                                         bool dual_buff_wait)
    {
        tensor::Tensors *outputs = new tensor::Tensors();
        err::Err e = forward(inputs, *outputs, copy_result, dual_buff_wait);
        if (e != err::ERR_NONE) {
            delete outputs;
            return nullptr;
        }
        return outputs;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // forward_image: CPU-side preprocess then forward
    // ──────────────────────────────────────────────────────────────────────────
    tensor::Tensors *NN_MaixCam::forward_image(image::Image &img,
                                               std::vector<float> mean,
                                               std::vector<float> scale,
                                               image::Fit fit,
                                               bool copy_result,
                                               bool clear_buff,
                                               bool chw)
    {
        if (!_loaded || !_data) {
            log::error("model not loaded");
            return nullptr;
        }
        MaixCamNNData *d = static_cast<MaixCamNNData *>(_data);

        if (d->input_num < 1) {
            log::error("model has no inputs");
            return nullptr;
        }

        // ── determine expected input shape ───────────────────────────────────
        const LayerInfo &in_info = d->inputs_info_cache[0];
        log::info("forward_image: input img=%dx%d fmt=%d, model input '%s' dtype=%d dims=%d",
                  img.width(), img.height(), (int)img.format(),
                  in_info.name.c_str(), (int)in_info.dtype, (int)in_info.shape.size());

        // support NCHW (4-dim) and CHW (3-dim) layouts
        int model_c = 0, model_h = 0, model_w = 0;
        if (in_info.shape.size() == 4) {
            model_c = in_info.shape[1];
            model_h = in_info.shape[2];
            model_w = in_info.shape[3];
            log::info("forward_image: NCHW layout -> N=%d C=%d H=%d W=%d",
                      in_info.shape[0], model_c, model_h, model_w);
        } else if (in_info.shape.size() == 3) {
            if (chw) {
                model_c = in_info.shape[0];
                model_h = in_info.shape[1];
                model_w = in_info.shape[2];
                log::info("forward_image: CHW layout -> C=%d H=%d W=%d", model_c, model_h, model_w);
            } else {
                model_h = in_info.shape[0];
                model_w = in_info.shape[1];
                model_c = in_info.shape[2];
                log::info("forward_image: HWC layout -> H=%d W=%d C=%d", model_h, model_w, model_c);
            }
        } else {
            log::error("forward_image: unsupported input shape dims: %d", (int)in_info.shape.size());
            return nullptr;
        }

        if (model_c <= 0 || model_h <= 0 || model_w <= 0) {
            log::error("forward_image: invalid model input shape C=%d H=%d W=%d",
                       model_c, model_h, model_w);
            return nullptr;
        }

        // ── resize image to model input size ─────────────────────────────────
        // Image::resize now uses a pure-C++ nearest-neighbour fallback that
        // avoids the OpenCV 3.2 RISC-V cv::resize(NEAREST) crash.
        image::Image *resized          = nullptr;
        bool          need_delete_resized = false;

        if (img.width() == model_w && img.height() == model_h) {
            resized = &img;
            log::info("forward_image: no resize needed, img already %dx%d",
                      model_w, model_h);
        } else {
            log::info("forward_image: resizing %dx%d -> %dx%d fit=%d",
                      img.width(), img.height(), model_w, model_h, (int)fit);
            resized = img.resize(model_w, model_h, fit);
            if (!resized) {
                log::error("forward_image: resize returned null (src %dx%d -> %dx%d fit=%d)",
                           img.width(), img.height(), model_w, model_h, (int)fit);
                return nullptr;
            }
            need_delete_resized = true;
            log::info("forward_image: resize done -> %dx%d fmt=%d",
                      resized->width(), resized->height(), (int)resized->format());
        }

        // ── convert to RGB888 if needed ───────────────────────────────────────
        image::Image *rgb_img = nullptr;
        bool need_delete_rgb = false;
        if (resized->format() != image::FMT_RGB888) {
            log::info("forward_image: converting fmt %d -> RGB888", (int)resized->format());
            rgb_img = resized->to_format(image::FMT_RGB888);
            if (!rgb_img) {
                log::error("forward_image: to_format(RGB888) returned null! src fmt=%d size=%dx%d",
                           (int)resized->format(), resized->width(), resized->height());
                if (need_delete_resized) { delete resized; resized = nullptr; }
                return nullptr;
            }
            need_delete_rgb = true;
            log::info("forward_image: format convert done -> %dx%d fmt=%d",
                      rgb_img->width(), rgb_img->height(), (int)rgb_img->format());
        } else {
            rgb_img = resized;
            log::info("forward_image: no format convert needed, already RGB888");
        }

        if (!rgb_img->data()) {
            log::error("forward_image: rgb_img->data() is null! size=%dx%d fmt=%d",
                       rgb_img->width(), rgb_img->height(), (int)rgb_img->format());
            if (need_delete_rgb)     { delete rgb_img;  rgb_img  = nullptr; }
            if (need_delete_resized) { delete resized;  resized  = nullptr; }
            return nullptr;
        }

        // ── [diag] save resized/formatted input for visual inspection ─────────
        {
            err::Err _se = rgb_img->save("/tmp/nn_input_debug.jpg", 90);
            log::info("forward_image: [diag] saved resized input -> /tmp/nn_input_debug.jpg "
                      "(%dx%d fmt=%d err=%d)",
                      rgb_img->width(), rgb_img->height(), (int)rgb_img->format(), (int)_se);
        }

        // ── get cvi input tensor reference early for fmt-based branching ──────
        CVI_TENSOR &ct        = d->input_tensors[0];
        size_t cvi_tensor_size = CVI_NN_TensorSize(&ct);
        int total_pixels       = model_c * model_h * model_w;
        const uint8_t *src     = static_cast<const uint8_t *>(rgb_img->data());

        log::info("forward_image: CVI input tensor '%s' fmt=%d cvi_size=%zu total_pixels=%d layout=%s",
                  ct.name ? ct.name : "?", (int)ct.fmt,
                  cvi_tensor_size, total_pixels, chw ? "CHW" : "HWC");

        void *cvi_dst = CVI_NN_TensorPtr(&ct);
        if (!cvi_dst) {
            log::error("forward_image: CVI_NN_TensorPtr returned null for input 0");
            if (need_delete_rgb)     { delete rgb_img;  rgb_img  = nullptr; }
            if (need_delete_resized) { delete resized;  resized  = nullptr; }
            return nullptr;
        }

        if (ct.fmt == CVI_FMT_INT8 || ct.fmt == CVI_FMT_UINT8) {
            // ── int8/uint8 path ───────────────────────────────────────────────
            // CVI asymmetric INT8 quantisation maps the float [0,1] input range
            // to [-128, 127].  The correct encoding is:
            //   q = (uint8_t pixel) - 128
            // NOT a raw bitwise reinterpret of the uint8 byte.
            log::info("forward_image: int8 preprocess path, building %d-byte int8 buf", total_pixels);
            int8_t *int8_buf = new int8_t[total_pixels];
            if (chw) {
                // HWC -> CHW reorder + pixel-128 encoding
                for (int c = 0; c < model_c; c++)
                    for (int h = 0; h < model_h; h++)
                        for (int w = 0; w < model_w; w++)
                            int8_buf[c * model_h * model_w + h * model_w + w] =
                                (int8_t)((int)src[(h * model_w + w) * model_c + c] - 128);
            } else {
                // HWC layout, pixel-128 encoding
                for (int i = 0; i < total_pixels; i++)
                    int8_buf[i] = (int8_t)((int)src[i] - 128);
            }
            log::info("forward_image: int8 preprocess done, sample [0]=%d [1]=%d [2]=%d",
                      (int)int8_buf[0], (int)int8_buf[1], (int)int8_buf[2]);

            // clean up image temporaries
            if (need_delete_rgb)     { delete rgb_img;  rgb_img  = nullptr; }
            if (need_delete_resized) { delete resized;  resized  = nullptr; }

            log::info("forward_image: memcpy %zu bytes to CVI input buffer @ %p",
                      (size_t)total_pixels, cvi_dst);
            memcpy(cvi_dst, int8_buf, (size_t)total_pixels);
            delete[] int8_buf;

        } else {
            // ── float32 path with optional mean/scale normalisation ────────────
            size_t float_buf_bytes = (size_t)(total_pixels * sizeof(float));
            log::info("forward_image: fp32 preprocess path, alloc %zu bytes, "
                      "normalize=%s mean.size=%d scale.size=%d",
                      float_buf_bytes,
                      (!mean.empty() && !scale.empty()) ? "yes" : "no",
                      (int)mean.size(), (int)scale.size());
            float *float_buf   = new float[total_pixels];
            bool do_normalize  = !mean.empty() && !scale.empty();
            bool mean_3        = mean.size()  >= 3;
            bool scale_3       = scale.size() >= 3;

            if (chw) {
                for (int c = 0; c < model_c; c++) {
                    float m = do_normalize ? (mean_3  ? mean[c]  : mean[0])  : 0.0f;
                    float s = do_normalize ? (scale_3 ? scale[c] : scale[0]) : 1.0f;
                    for (int h = 0; h < model_h; h++)
                        for (int w = 0; w < model_w; w++) {
                            uint8_t pv = src[(h * model_w + w) * model_c + c];
                            float_buf[c * model_h * model_w + h * model_w + w] =
                                do_normalize ? ((float)pv - m) * s : (float)pv;
                        }
                }
            } else {
                for (int h = 0; h < model_h; h++)
                    for (int w = 0; w < model_w; w++)
                        for (int c = 0; c < model_c; c++) {
                            uint8_t pv = src[(h * model_w + w) * model_c + c];
                            float m = do_normalize ? (mean_3  ? mean[c]  : mean[0])  : 0.0f;
                            float s = do_normalize ? (scale_3 ? scale[c] : scale[0]) : 1.0f;
                            float_buf[(h * model_w + w) * model_c + c] =
                                do_normalize ? ((float)pv - m) * s : (float)pv;
                        }
            }
            log::info("forward_image: fp32 preprocess done, sample [0]=%.3f [1]=%.3f [2]=%.3f",
                      float_buf[0], float_buf[1], float_buf[2]);

            // clean up image temporaries
            if (need_delete_rgb)     { delete rgb_img;  rgb_img  = nullptr; }
            if (need_delete_resized) { delete resized;  resized  = nullptr; }

            if (cvi_tensor_size != float_buf_bytes)
                log::warn("forward_image: CVI tensor size=%zu != float buf size=%zu, copying min",
                          cvi_tensor_size, float_buf_bytes);
            size_t copy_bytes = std::min(float_buf_bytes, cvi_tensor_size);
            log::info("forward_image: memcpy %zu bytes to CVI input buffer @ %p",
                      copy_bytes, cvi_dst);
            memcpy(cvi_dst, float_buf, copy_bytes);
            delete[] float_buf;
        }

        // ── run inference ─────────────────────────────────────────────────────
        log::info("forward_image: calling CVI_NN_Forward ...");
        CVI_RC rc = CVI_NN_Forward(d->model,
                                   d->input_tensors,  d->input_num,
                                   d->output_tensors, d->output_num);
        if (rc != CVI_RC_SUCCESS) {
            log::error("forward_image: CVI_NN_Forward failed rc=%d", rc);
            return nullptr;
        }
        log::info("forward_image: CVI_NN_Forward done");

        // ── pack output tensors ───────────────────────────────────────────────
        tensor::Tensors *outputs = new tensor::Tensors();
        for (int i = 0; i < d->output_num; i++) {
            CVI_TENSOR &oct = d->output_tensors[i];
            const LayerInfo &li = d->outputs_info_cache[i];
            void *out_src = CVI_NN_TensorPtr(&oct);
            log::info("forward_image: output[%d] '%s' fmt=%d size=%zu ptr=%p",
                      i, li.name.c_str(), (int)oct.fmt, CVI_NN_TensorSize(&oct), out_src);
            if (!out_src) {
                log::error("forward_image: CVI_NN_TensorPtr null for output %d '%s'",
                           i, li.name.c_str());
                delete outputs;
                return nullptr;
            }
            tensor::Tensor *t = new tensor::Tensor(li.shape, li.dtype, out_src, copy_result);
            outputs->add_tensor(li.name, t, false, true);
        }
        log::info("forward_image: all %d outputs packed", d->output_num);
        return outputs;
    }

} // namespace maix::nn