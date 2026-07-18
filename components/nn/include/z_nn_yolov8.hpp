/**
 * @author neucrack@sipeed
 * @copyright Sipeed Ltd 2024-
 * @license Apache 2.0
 * @update 2024.6.7: Add yolov8 support.
 */

#pragma once
#include "maix_basic.hpp"
#include "z_nn.hpp"
#include "z_image.hpp"
#include "z_nn_F.hpp"
#include "z_nn_object.hpp"
#include <math.h>
#include "z_nn_yolo11.hpp"

namespace maix::nn
{
    /**
     * YOLOv8 class
     * @maixpy maix.nn.YOLOv8
     */
    class YOLOv8 : public YOLO11
    {
    public:
        /**
         * Constructor of YOLOv8 class
         * @param model model path, default empty, you can load model later by load function.
         * @param[in] dual_buff prepare dual input output buffer to accelarate forward, that is, when NPU is forwarding we not wait and prepare the next input buff.
         *                      If you want to ensure every time forward output the input's result, set this arg to false please.
         *                      Default true to ensure speed.
         * @throw If model arg is not empty and load failed, will throw err::Exception.
         * @maixpy maix.nn.YOLOv8.__init__
         * @maixcdk maix.nn.YOLOv8.YOLOv8
         */
        YOLOv8(const string &model = "", bool dual_buff = true)
            : YOLO11(model, "yolov8", dual_buff)
        {
        }

        /**
         * Load model from file
         * @param model Model path want to load
         * @return err::Err
         * @maixpy maix.nn.YOLOv8.load
         */
        err::Err load(const string &model) { return YOLO11::load(model); }

        /**
         * Detect objects from image
         * @maixpy maix.nn.YOLOv8.detect
         */
        nn::Objects *detect(image::Image &img, float conf_th = 0.5, float iou_th = 0.45,
                            maix::image::Fit fit = maix::image::FIT_CONTAIN, float keypoint_th = 0.5, int sort = 0)
        {
            return YOLO11::detect(img, conf_th, iou_th, fit, keypoint_th, sort);
        }

        /**
         * Get model input size
         * @maixpy maix.nn.YOLOv8.input_size
         */
        image::Size input_size() { return YOLO11::input_size(); }

        /**
         * Get model input width
         * @maixpy maix.nn.YOLOv8.input_width
         */
        int input_width() { return YOLO11::input_width(); }

        /**
         * Get model input height
         * @maixpy maix.nn.YOLOv8.input_height
         */
        int input_height() { return YOLO11::input_height(); }

        /**
         * Get input image format
         * @maixpy maix.nn.YOLOv8.input_format
         */
        image::Format input_format() { return YOLO11::input_format(); }

        /**
         * Draw pose keypoints on image
         * @maixpy maix.nn.YOLOv8.draw_pose
         */
        void draw_pose(image::Image &img, std::vector<int> points, int radius = 4, image::Color color = image::COLOR_RED,
                       const std::vector<image::Color> &colors = std::vector<image::Color>(), bool body = true, bool close = false)
        {
            YOLO11::draw_pose(img, points, radius, color, colors, body, close);
        }

        /**
         * Draw segmentation on image
         * @maixpy maix.nn.YOLOv8.draw_seg_mask
         */
        void draw_seg_mask(image::Image &img, int x, int y, image::Image &seg_mask, int threshold = 127)
        {
            YOLO11::draw_seg_mask(img, x, y, seg_mask, threshold);
        }
    };

} // namespace maix::nn
