/**
 * @author neucrack@sipeed, lxowalle@sipeed
 * @copyright Sipeed Ltd 2023-
 * @license Apache 2.0
 * @update 2023.9.8: Add framework, create this file.
 */

#include "maix_image.hpp"
#include "opencv2/opencv.hpp"
#include "opencv2/freetype.hpp"
#include <map>
#include <valarray>
#include <vector>
#include <string>
#include <array>
#include <sched.h>
#include <thread>
#include "omp.h"
#ifdef PLATFORM_MAIXCAM
#include "sophgo_middleware.hpp"
#elif defined(PLATFORM_MAIXCAM2)
#include "ax_middleware.hpp"
using namespace maix::middleware;
#endif
#include <omp.h>

namespace maix::image
{

    static void _get_cv_format_color(image::Format _format, const image::Color &color_in, int *ch_format, cv::Scalar &cv_color);

    static bool path_is_format(const std::string &str, const std::string ext)
    {
        std::string lower_path(str);
        std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
                       [](unsigned char c)
                       { return std::tolower(c); });

        if (lower_path.size() >= ext.size() && lower_path.compare(lower_path.size() - ext.size(), ext.size(), ext) == 0)
        {
            return true;
        }
        return false;
    }

    static uint8_t _get_char_size(const uint8_t c)
    {
        if ((c & 0x80) == 0x00)
        {
            // 0xxxxxxx: 1 byte character
            return 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            // 110xxxxx: 2 byte character
            return 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            // 1110xxxx: 3 byte character
            return 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            // 11110xxx: 4 byte character
            return 4;
        }
        else
        {
            // Invalid UTF-8 start byte, default to 1
            return 1;
        }
    }

    static image::Image *_mat_to_image(const cv::Mat &mat, image::Format format, void *buff, int buff_size, bool copy_data = true)
    {
        image::Image *img = nullptr;
        if (buff)
        {
            if (buff_size < image::fmt_size[format])
            {
                log::error("convert format failed, buffer size not enough, need %d, but %d\n", image::fmt_size[format], buff_size);
                throw err::Exception(err::ERR_ARGS, "convert format failed, buffer size not enough");
            }
            img = new image::Image(mat.cols, mat.rows, format, (uint8_t *)buff, mat.cols * mat.rows * image::fmt_size[format], false);
        }
        else
            img = new image::Image(mat.cols, mat.rows, format);
        if (copy_data)
            memcpy(img->data(), mat.data, mat.cols * mat.rows * image::fmt_size[format]);
        return img;
    }

    static image::Image *_new_image(int w, int h, image::Format format, void *buff, int buff_size)
    {
        image::Image *img = nullptr;
        if (buff)
        {
            if (buff_size < image::fmt_size[format])
            {
                log::error("convert format failed, buffer size not enough, need %d, but %d\n", image::fmt_size[format], buff_size);
                throw err::Exception(err::ERR_ARGS, "convert format failed, buffer size not enough");
            }
            img = new image::Image(w, h, format, (uint8_t *)buff, w * h * image::fmt_size[format], false);
        }
        else
            img = new image::Image(w, h, format);
        return img;
    }

    static void _cv_rgb_nv21(const cv::Mat &rgb, cv::Mat &nv21, int width, int height, bool bgr = false)
    {
        cv::cvtColor(rgb, nv21, bgr ? cv::COLOR_BGR2YUV_YV12 : cv::COLOR_RGB2YUV_YV12);
        int nv_len = width * height / 2;
        int half_len = nv_len / 2;
        int offset = width * height;
        uint8_t *nv21_data = (uint8_t *)nv21.data;
        uint8_t *uv_temp = (uint8_t *)malloc(nv_len);
        err::check_null_raise(uv_temp, "malloc uv_temp failed");
        memcpy(uv_temp, nv21_data + offset, nv_len);
        #pragma omp parallel for
        for (int i = 0; i < half_len; i++)
        {
            nv21_data[offset + i * 2] = uv_temp[i];                // V
            nv21_data[offset + i * 2 + 1] = uv_temp[half_len + i]; // U
        }
        if (uv_temp)
            free(uv_temp);
    }

    image::Image *load(const char *path, image::Format format)
    {
        cv::Mat mat;
        if (format == image::FMT_BGR888 || format == image::FMT_RGB888)
        {
            mat = cv::imread(path);
            if (mat.empty())
                return nullptr;
            if (format == image::FMT_RGB888)
                cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);
        }
        else
        {
            mat = cv::imread(path, cv::IMREAD_UNCHANGED);
            if (mat.empty())
                return nullptr;
            if (mat.channels() == 1)
            {
                switch (format)
                {
                case image::FMT_GRAYSCALE:
                    break;
                case image::FMT_RGB888:
                    cv::cvtColor(mat, mat, cv::COLOR_GRAY2RGB);
                    break;
                case image::FMT_RGBA8888:
                    cv::cvtColor(mat, mat, cv::COLOR_GRAY2RGBA);
                    break;
                case image::FMT_BGR888:
                    cv::cvtColor(mat, mat, cv::COLOR_GRAY2BGR);
                    break;
                case image::FMT_BGRA8888:
                    cv::cvtColor(mat, mat, cv::COLOR_GRAY2BGRA);
                    break;
                default:
                    log::error("load image failed, can't convert grayscale to format %d\n", format);
                    return nullptr;
                }
            }
            else if (mat.channels() == 3)
            {
                switch (format)
                {
                case image::FMT_GRAYSCALE:
                    cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
                    break;
                case image::FMT_RGB888:
                    cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);
                    break;
                case image::FMT_RGBA8888:
                    cv::cvtColor(mat, mat, cv::COLOR_BGR2RGBA);
                    break;
                case image::FMT_BGR888:
                    break;
                case image::FMT_BGRA8888:
                    cv::cvtColor(mat, mat, cv::COLOR_BGR2BGRA);
                    break;
                default:
                    log::error("load image failed, can't convert bgr to format %d\n", format);
                    return nullptr;
                }
            }
            else if (mat.channels() == 4)
            {
                switch (format)
                {
                case image::FMT_GRAYSCALE:
                    cv::cvtColor(mat, mat, cv::COLOR_BGRA2GRAY);
                    break;
                case image::FMT_RGB888:
                    cv::cvtColor(mat, mat, cv::COLOR_BGRA2RGB);
                    break;
                case image::FMT_RGBA8888:
                    cv::cvtColor(mat, mat, cv::COLOR_BGRA2RGBA);
                    break;
                case image::FMT_BGR888:
                    cv::cvtColor(mat, mat, cv::COLOR_BGRA2BGR);
                    break;
                case image::FMT_BGRA8888:
                    break;
                default:
                    log::error("load image failed, can't convert bgra to format %d\n", format);
                    return nullptr;
                }
            }
            else
            {
                log::error("load image failed, channels not support: %d\n", mat.channels());
                return nullptr;
            }
        }
        image::Image *img = new image::Image(mat.cols, mat.rows, format);
        memcpy(img->data(), mat.data, mat.cols * mat.rows * mat.channels());
        return img;
    }

    image::Image *from_bytes(int width, int height, image::Format format, Bytes *data, bool copy)
    {
        // _create_image(width, height, format, data->data, data->size(), copy);
        return new image::Image(width, height, format, data->data, data->size(), copy);
    }

    static std::map<std::string, cv::Ptr<cv::freetype::FreeType2>> fonts_info;
    static std::map<std::string, int> fonts_size_info;
    static std::string curr_font_name = "hershey_plain";
    static int curr_font_id = cv::FONT_HERSHEY_PLAIN; // -1 if user custom font, else opencv's HersheyFonts id

    static void add_default_fonts(std::map<std::string, cv::Ptr<cv::freetype::FreeType2>> &fonts_info)
    {
        if (!fonts_info.empty())
            return;
        fonts_info["hershey_simplex"] = cv::Ptr<cv::freetype::FreeType2>();
        fonts_info["hershey_plain"] = cv::Ptr<cv::freetype::FreeType2>();
        fonts_info["hershey_duplex"] = cv::Ptr<cv::freetype::FreeType2>();
        fonts_info["hershey_complex"] = cv::Ptr<cv::freetype::FreeType2>();
        fonts_info["hershey_triplex"] = cv::Ptr<cv::freetype::FreeType2>();
        fonts_info["hershey_complex_small"] = cv::Ptr<cv::freetype::FreeType2>();
        fonts_info["hershey_script_simplex"] = cv::Ptr<cv::freetype::FreeType2>();
    }

    static int get_default_fonts_id(const std::string &name)
    {
        if (name == "hershey_simplex")
            return cv::FONT_HERSHEY_SIMPLEX;
        else if (name == "hershey_plain")
            return cv::FONT_HERSHEY_PLAIN;
        else if (name == "hershey_duplex")
            return cv::FONT_HERSHEY_DUPLEX;
        else if (name == "hershey_complex")
            return cv::FONT_HERSHEY_COMPLEX;
        else if (name == "hershey_triplex")
            return cv::FONT_HERSHEY_TRIPLEX;
        else if (name == "hershey_complex_small")
            return cv::FONT_HERSHEY_COMPLEX_SMALL;
        else if (name == "hershey_script_simplex")
            return cv::FONT_HERSHEY_SCRIPT_SIMPLEX;
        else
            return -1;
    }

    err::Err load_font(const std::string &name, const char *path, int size)
    {
        add_default_fonts(fonts_info);
        // use opencv to load freetype font and store object in global variable
        cv::Ptr<cv::freetype::FreeType2> ft2 = cv::freetype::createFreeType2();
        if (!ft2)
        {
            log::error("load font failed\n");
            return err::ERR_ARGS;
        }
        ft2->loadFontData(path, 0);
        fonts_info[name] = ft2;
        fonts_size_info[name] = size;
        return err::ERR_NONE;
    }

    err::Err set_default_font(const std::string &name)
    {
        // if name in fonts_info
        if (fonts_info.find(name) == fonts_info.end())
        {
            log::error("font %d not load\n", name.c_str());
            return err::ERR_ARGS;
        }
        curr_font_name = name;
        curr_font_id = get_default_fonts_id(name);
        return err::ERR_NONE;
    }

    std::vector<std::string> *fonts()
    {
        std::vector<std::string> *fonts = new std::vector<std::string>;
        add_default_fonts(fonts_info);
        for (auto &font : fonts_info)
        {
            fonts->push_back(font.first);
        }
        return fonts;
    }

    static void _get_text_size(cv::Size &size, const std::string &text, const std::string &font_name, int font_id, float scale, int thickness)
    {
        int baseLine = 0;
        if (font_id == -1)
        {
            int font_height = fonts_size_info[font_name];
            cv::Ptr<cv::freetype::FreeType2> ft2 = fonts_info[font_name];
            if (ft2 == cv::Ptr<cv::freetype::FreeType2>())
            {
                log::error("font %d not load\n", font_name.c_str());
                throw std::runtime_error("font not load");
            }
            size = ft2->getTextSize(text, scale * font_height, thickness, &baseLine);
            if (thickness > 0)
                baseLine += thickness;
            size.height = size.height + baseLine;
        }
        else
        {
            size = cv::getTextSize(text, font_id, scale, thickness > 0 ? thickness : -thickness, &baseLine);
            baseLine += baseLine > 0 ? 0 : -thickness;
            size.height += baseLine;
        }
    }

    static void _put_text(cv::Mat &img, const std::string &text, const cv::Point &point,
                          const cv::Scalar &color, float scale, int thickness, const std::string &font_name, int font_id)
    {
        if (font_id == -1)
        {
            cv::Ptr<cv::freetype::FreeType2> ft2 = fonts_info[font_name];
            if (ft2 == cv::Ptr<cv::freetype::FreeType2>())
            {
                log::error("font %d not load\n", font_name.c_str());
                throw std::runtime_error("font not load");
            }
            // point from left top to left center
            const std::string text_tmp(text, 0, 1);
            cv::Point point_tmp(point.x, point.y + ft2->getTextSize(text, scale * fonts_size_info[font_name], thickness, nullptr).height);
            ft2->putText(img, text, point_tmp, scale * fonts_size_info[font_name], color, thickness, cv::LINE_AA, true);
        }
        else
        {
            // left top corner to point left bottom corner by first char height
            const std::string text_tmp(text, 0, 1);
            int baseLine = 0;
            cv::Point point_tmp(point.x, point.y + cv::getTextSize(text_tmp, font_id, scale, thickness > 0 ? thickness : -thickness, &baseLine).height);
            cv::putText(img, text, point_tmp, font_id, scale, color, thickness > 0 ? thickness : -thickness, cv::LINE_AA, false);
        }
    }

    void Image::_create_image(int width, int height, image::Format format, uint8_t *data, int data_size, bool copy, const image::Color &bg)
    {
        _format = format;
        _width = width;
        _height = height;
        if (width <= 0 || height <= 0)
            throw err::Exception(err::ERR_ARGS, "image width and height should > 0");

        if (_format > Format::FMT_COMPRESSED_MIN)
        {
            if (!data || data_size < 0)
                throw err::Exception(err::ERR_ARGS, "image data and data_size are incorrect");
            _data_size = data_size;
        }
        else
        {
            // not use data_size, uncompressed image only use fiexed size.
            int size_calc = width * height * image::fmt_size[format];
            if (data_size > 0 && data_size != size_calc)
            {
                log::error("data_size not match image content size, data_size: %d, image content size: %d\n", data_size, size_calc);
                throw err::Exception(err::ERR_ARGS, "data_size not match image content size");
            }
            _data_size = size_calc;
        }

        if (!data)
        {
            _actual_data = malloc(_data_size + 0x1000);
            if (!_actual_data)
                throw err::Exception(err::ERR_NO_MEM, "malloc image data failed");
            _data = (void *)(((uint64_t)_actual_data + 0x1000) & ~0xFFF);
            // set background color
            if(bg.format == image::FMT_INVALID)
            {
                // do nothing
            }
            else if(bg.format == image::FMT_GRAYSCALE)
            {
                memset(_data, bg.gray, _data_size);
            }
            else if ((bg.format == image::FMT_RGB888 || bg.format == image::FMT_BGR888 || bg.format == image::FMT_RGBA8888 || bg.format == image::FMT_BGRA8888) &&
                (_format == image::FMT_RGB888 || _format == image::FMT_BGR888))
            {
                if(bg.r == bg.g && bg.g == bg.b)
                {
                    memset(_data, bg.r, _data_size);
                }
                else
                {
                    uint8_t *p = (uint8_t *)_data;
                    #pragma omp parallel for
                    for (int i = 0; i < _width * _height; i++)
                    {
                        p[i * 3] = bg.r;
                        p[i * 3 + 1] = bg.g;
                        p[i * 3 + 2] = bg.b;
                    }
                }
            }
            else if ((bg.format == image::FMT_RGB888 || bg.format == image::FMT_BGR888 || bg.format == image::FMT_RGBA8888 || bg.format == image::FMT_BGRA8888) &
                (_format == image::FMT_RGBA8888 || _format == image::FMT_BGRA8888))
            {
                if(bg.r == bg.g && bg.g == bg.b && bg.alpha == 1)
                {
                    memset(_data, bg.r, _data_size);
                }
                else
                {
                    uint8_t *p = (uint8_t *)_data;
                    #pragma omp parallel for
                    for (int i = 0; i < _width * _height; i++)
                    {
                        p[i * 4] = bg.r;
                        p[i * 4 + 1] = bg.g;
                        p[i * 4 + 2] = bg.b;
                        p[i * 4 + 3] = bg.alpha;
                    }
                }
            }
            else
            {
                free(_actual_data);
                _actual_data = NULL;
                _data = NULL;
                log::error("image bg format not support, format: %d\n", bg.format);
                throw err::Exception(err::ERR_ARGS, "image bg format not support, use grayscale(recommend)/rgb/rgba");
            }
            _is_malloc = true;
        }
        else
        {
            if (!copy)
            {
                _data = data;
                _actual_data = _data;
                _is_malloc = false;
            }
            else
            {
                _actual_data = malloc(_data_size + 0x1000);
                if (!_actual_data)
                    throw std::bad_alloc();
                _data = (void *)(((uint64_t)_actual_data + 0x1000) & ~0xFFF);
                memcpy(_data, data, _data_size);
                // log::debug("malloc image data\n");
                _is_malloc = true;
            }
        }
    }

    Image::Image(int width, int height, image::Format format, uint8_t *data, int data_size, bool copy, const image::Color &bg)
    {
        _create_image(width, height, format, data, data_size, copy, bg);
    }

    Image::Image(int width, int height, image::Format format, const image::Color &bg)
    // Image::Image(int width, int height, image::Format format, Bytes *data, bool copy)
    {
        // if(!data)
        // {
        _create_image(width, height, format, nullptr, 0, false, bg);
        // return;
        // }
        // _create_image(width, height, format, data->data, data->size(), copy);
    }

    Image::~Image()
    {
        if (_is_malloc)
        {
            // log::debug("free image data\n");
            free(_actual_data);
            _actual_data = NULL;
            _data = NULL;
        }
    }

    err::Err Image::update(int width, int height, image::Format format, uint8_t *data, int data_size, bool copy)
    {
        if (_actual_data && _is_malloc)
        {
            // log::debug("free image data\n");
            free(_actual_data);
            _actual_data = NULL;
            _data = NULL;
        }
        _create_image(width, height, format, data, data_size, copy);
        return err::ERR_NONE;
    }

    void Image::operator=(const image::Image &img)
    {
        if (_data)
        {
            if (_is_malloc)
            {
                log::info("free _actual_data");
                free(_actual_data);
                _actual_data = NULL;
                _data = NULL;
            }
            else
                throw err::Exception(err::ERR_NOT_IMPL, "not support copy image to not alloc data image");
        }
        _format = img._format;
        _width = img._width;
        _height = img._height;
        _data_size = _width * _height * image::fmt_size[_format];
        _actual_data = malloc(_data_size + 0x1000);
        if (!_actual_data)
            throw std::bad_alloc();
        _data = (void *)(((uint64_t)_actual_data + 0x1000) & ~0xFFF);
        memcpy(_data, img._data, _data_size);
        _is_malloc = true;
        // log::debug("malloc image data\n");
    }

    std::string Image::__str__()
    {
        char buf[128];
        sprintf(buf, "Image(%d, %d, %s), data size: %d", _width, _height, fmt_names[_format].c_str(), _data_size);
        return std::string(buf);
    }

    Bytes *Image::to_bytes(bool copy)
    {
        if (copy)
            return new Bytes((uint8_t *)_data, _data_size, true, true);
        return new Bytes((uint8_t *)_data, _data_size, false, false);
    }

    tensor::Tensor *Image::to_tensor(bool chw, bool copy)
    {
        void *data = _data;
        std::vector<int> shape;
        tensor::Tensor *t = nullptr;
        if (_format == image::FMT_GRAYSCALE)
            shape = {_height, _width};
        else if (_format < image::FMT_YUV422SP)
            shape = chw ? (std::vector<int>){(int)image::fmt_size[_format], _height, _width} : (std::vector<int>){_height, _width, (int)image::fmt_size[_format]};
        else if (_format == image::FMT_YUV422SP || _format == image::FMT_YUV422P)
            shape = chw ? (std::vector<int>){2, _height, _width} : (std::vector<int>){_height, _width, 2};
        else if (_format == image::FMT_YUV420SP || _format == image::FMT_YUV420P || _format == image::FMT_YVU420SP || _format == image::FMT_YVU420P)
            shape = {(int)(_height * 1.5), _width};
        else
            throw std::runtime_error("not support format");
        if (copy)
        {
            t = new tensor::Tensor(shape, tensor::UINT8, nullptr);
            memcpy(t->data(), data, t->size_int() * tensor::dtype_size[t->dtype()]);
        }
        else
        {
            t = new tensor::Tensor(shape, tensor::UINT8, data);
        }
        return t;
    }

    image::Image *Image::to_format(const image::Format &format, void *buff, size_t buff_size)
    {
        if (_format == format)
        {
            log::error("convert format failed, already the format %d\n", format);
            throw err::Exception(err::ERR_ARGS, "convert format failed, already the format");
        }
        cv::Mat src(_format > FMT_COMPRESSED_MIN ? 1 : _height, _format > FMT_COMPRESSED_MIN ? _data_size : _width, CV_8UC((int)image::fmt_size[_format]), _data);
        cv::ColorConversionCodes cvt_code;

        // special for convert to jpeg and png
        if (format == image::FMT_JPEG) // compress
        {
            return this->to_jpeg(80, buff, buff_size);
        }
        else if (format == image::FMT_PNG)
        {
            image::Image *p_img = nullptr;
            cv::Mat *p_src = &src;
            bool src_alloc = false;
            if (_format != Format::FMT_BGR888 && _format != Format::FMT_BGRA8888)
            {
                p_img = to_format(image::FMT_BGRA8888);
                p_src = new cv::Mat(p_img->_height, p_img->_width, CV_8UC((int)image::fmt_size[image::FMT_BGRA8888]), p_img->_data);
                src_alloc = true;
            }
            cv::InputArray input(*p_src);
            std::vector<uchar> png_buff;
            std::vector<int> params;
            params.push_back(cv::IMWRITE_PNG_COMPRESSION);
            params.push_back(3);
            cv::imencode(".png", input, png_buff, params);
            image::Image *img;
            if (buff)
            {
                if (buff_size < png_buff.size())
                {
                    if (src_alloc)
                    {
                        delete p_img;
                        delete p_src;
                    }
                    throw err::Exception(err::ERR_ARGS, "convert format failed, buffer size not enough");
                }
                memcpy(buff, png_buff.data(), png_buff.size());
                img = new image::Image(src.cols, src.rows, format, (uint8_t *)buff, png_buff.size(), false);
            }
            else
                img = new image::Image(src.cols, src.rows, format, png_buff.data(), png_buff.size(), true);
            if (src_alloc)
            {
                delete p_img;
                delete p_src;
            }
            return img;
        }

        // jpeg/png to other format
        if (_format == image::FMT_JPEG || _format == image::FMT_PNG)
        {
            switch (format)
            {
            case image::FMT_GRAYSCALE:
            {
                cv::Mat dst = cv::imdecode(src, cv::IMREAD_GRAYSCALE);
                if (dst.empty())
                    throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                return _mat_to_image(dst, format, buff, buff_size);
            }
            case image::FMT_BGR888:
            {
                cv::Mat dst = cv::imdecode(src, cv::IMREAD_COLOR);
                if (dst.empty())
                    throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                return _mat_to_image(dst, format, buff, buff_size);
            }
            case image::FMT_RGB888:
            {
                cv::Mat dst = cv::imdecode(src, cv::IMREAD_COLOR);
                if (dst.empty())
                    throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                cv::cvtColor(dst, dst, cv::COLOR_BGR2RGB);
                return _mat_to_image(dst, format, buff, buff_size);
            }
            case image::FMT_RGBA8888:
            {
                if (_format == FMT_PNG)
                {
                    cv::Mat dst = cv::imdecode(src, cv::IMREAD_UNCHANGED);
                    if (dst.empty())
                        throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                    cv::cvtColor(dst, dst, cv::COLOR_BGRA2RGBA);
                    return _mat_to_image(dst, format, buff, buff_size);
                }
                cv::Mat bgr = cv::imdecode(src, cv::IMREAD_COLOR);
                if (bgr.empty())
                    throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                image::Image *img = _new_image(bgr.cols, bgr.rows, format, buff, buff_size);
                cv::Mat dst(bgr.rows, bgr.cols, CV_8UC4, img->data());
                cv::cvtColor(bgr, dst, cv::COLOR_BGR2RGBA);
                return img;
            }
            case image::FMT_BGRA8888:
            {
                if (_format == FMT_PNG)
                {
                    cv::Mat dst = cv::imdecode(src, cv::IMREAD_UNCHANGED);
                    if (dst.empty())
                        throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                    return _mat_to_image(dst, format, buff, buff_size);
                }
                cv::Mat bgr = cv::imdecode(src, cv::IMREAD_COLOR);
                if (bgr.empty())
                    throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                image::Image *img = _new_image(bgr.cols, bgr.rows, format, buff, buff_size);
                cv::Mat dst(bgr.rows, bgr.cols, CV_8UC4, img->data());
                cv::cvtColor(bgr, dst, cv::COLOR_BGR2BGRA);
                return img;
            }
            case image::FMT_YVU420SP:
            {
                cv::Mat dst = cv::imdecode(src, cv::IMREAD_COLOR);
                if (dst.empty())
                    throw err::Exception(err::ERR_ARGS, "decode jpeg failed");
                image::Image *img = _mat_to_image(dst, format, buff, buff_size, false);
                cv::Mat dst_nv21(img->height() + img->height() / 2, img->width(), CV_8UC1, img->data());
                _cv_rgb_nv21(dst, dst_nv21, img->width(), img->height(), true);
                return _mat_to_image(dst, format, buff, buff_size);
            }
            default:
                throw err::Exception(err::ERR_NOT_IMPL, "not support format");
            }
            throw err::Exception(err::ERR_ARGS, "not wupport format");
        }

        // RGB BGR BGRA RGBA GRAYSCALE YUV transform
        switch (_format)
        {
        case image::FMT_RGB888:
            switch (format)
            {
            case image::FMT_GRAYSCALE:
                cvt_code = cv::COLOR_RGB2GRAY;
                break;
            case image::FMT_BGR888:
                cvt_code = cv::COLOR_RGB2BGR;
                break;
            case image::FMT_RGBA8888:
                cvt_code = cv::COLOR_RGB2RGBA;
                break;
            case image::FMT_BGRA8888:
                cvt_code = cv::COLOR_RGB2BGRA;
                break;
            case image::FMT_YVU420SP:
                cvt_code = cv::COLOR_RGB2YUV_YV12;
                break;
            default:
                log::error("convert format failed, can't convert rgb to format %d\n", format);
                throw err::Exception(err::ERR_NOT_IMPL, "not support format");
            }
            break;
        case image::FMT_BGR888:
            switch (format)
            {
            case image::FMT_GRAYSCALE:
                cvt_code = cv::COLOR_BGR2GRAY;
                break;
            case image::FMT_RGB888:
                cvt_code = cv::COLOR_BGR2RGB;
                break;
            case image::FMT_RGBA8888:
                cvt_code = cv::COLOR_BGR2RGBA;
                break;
            case image::FMT_BGRA8888:
                cvt_code = cv::COLOR_BGR2BGRA;
                break;
            case image::FMT_YVU420SP:
                cvt_code = cv::COLOR_BGR2YUV_YV12;
                break;
            default:
                throw err::Exception(err::ERR_NOT_IMPL, "not support format");
            }
            break;
        case image::FMT_RGBA8888:
            switch (format)
            {
            case image::FMT_GRAYSCALE:
                cvt_code = cv::COLOR_RGBA2GRAY;
                break;
            case image::FMT_RGB888:
                cvt_code = cv::COLOR_RGBA2RGB;
                break;
            case image::FMT_BGR888:
                cvt_code = cv::COLOR_RGBA2BGR;
                break;
            case image::FMT_BGRA8888:
                cvt_code = cv::COLOR_RGBA2BGRA;
                break;
            case image::FMT_YVU420SP:
                cvt_code = cv::COLOR_RGB2YUV_YV12;
                break;
            default:
                throw err::Exception(err::ERR_NOT_IMPL, "not support format");
            }
            break;
        case image::FMT_BGRA8888:
            switch (format)
            {
            case image::FMT_GRAYSCALE:
                cvt_code = cv::COLOR_BGRA2GRAY;
                break;
            case image::FMT_RGB888:
                cvt_code = cv::COLOR_BGRA2RGB;
                break;
            case image::FMT_BGR888:
                cvt_code = cv::COLOR_BGRA2BGR;
                break;
            case image::FMT_RGBA8888:
                cvt_code = cv::COLOR_BGRA2RGBA;
                break;
            case image::FMT_YVU420SP:
                cvt_code = cv::COLOR_BGR2YUV_YV12;
                break;
            default:
                throw err::Exception(err::ERR_NOT_IMPL, "not support format");
            }
            break;
        case image::FMT_GRAYSCALE:
            switch (format)
            {
            case image::FMT_RGB888:
                cvt_code = cv::COLOR_GRAY2RGB;
                break;
            case image::FMT_BGR888:
                cvt_code = cv::COLOR_GRAY2BGR;
                break;
            case image::FMT_RGBA8888:
                cvt_code = cv::COLOR_GRAY2RGBA;
                break;
            case image::FMT_BGRA8888:
                cvt_code = cv::COLOR_GRAY2BGRA;
                break;
            case image::FMT_YVU420SP:
                cvt_code = cv::COLOR_BGR2YUV_YV12;
                break;
            default:
                throw err::Exception(err::ERR_NOT_IMPL, "not support format");
            }
            break;
        case image::FMT_YVU420SP:
            switch (format)
            {
            case image::FMT_GRAYSCALE:
            {
                auto img = new image::Image(_width, _height, image::FMT_GRAYSCALE);
                memcpy(img->data(), _data, _width * _height);
                return img;
            }
            break;
            default:
                throw err::Exception(err::ERR_NOT_IMPL, "not support format");
            }
            break;
        default:
            throw err::Exception(err::ERR_NOT_IMPL, "not support format");
        }
        image::Image *img;
        if (buff)
        {
            if (buff_size < image::fmt_size[format])
            {
                log::error("convert format failed, buffer size not enough, need %d, but %d\n", image::fmt_size[format], buff_size);
                throw err::Exception(err::ERR_ARGS, "convert format failed, buffer size not enough");
            }
            img = new image::Image(src.cols, src.rows, format, (uint8_t *)buff, src.cols * src.rows * image::fmt_size[format], false);
        }
        else
            img = new image::Image(src.cols, src.rows, format);

        if (_format == image::FMT_GRAYSCALE && format == image::FMT_YVU420SP)
        {
            cv::Mat dst(src.rows * 3 / 2, src.cols, CV_8UC((int)image::fmt_size[format]), img->data());
            int nv_len = src.cols * src.rows / 2;
            int offset = src.cols * src.rows;
            memcpy(dst.data, src.data, src.cols * src.rows);
            memset(dst.data + offset, 128, nv_len);
        }
        else if (format == image::FMT_YVU420SP)
        {
            cv::Mat dst(src.rows * 3 / 2, src.cols, CV_8UC((int)image::fmt_size[format]), img->data());
            cv::cvtColor(src, dst, cvt_code);
            int nv_len = src.cols * src.rows / 2;
            int half_len = nv_len / 2;
            int offset = src.cols * src.rows;
            uint8_t *nv21 = (uint8_t *)img->data();
            uint8_t *uv_temp = (uint8_t *)malloc(nv_len);
            err::check_null_raise(uv_temp, "malloc uv_temp failed");
            memcpy(uv_temp, nv21 + offset, nv_len);
            #pragma omp parallel for
            for (int i = 0; i < half_len; i++)
            {
                nv21[offset + i * 2] = uv_temp[i];                // V
                nv21[offset + i * 2 + 1] = uv_temp[half_len + i]; // U
            }
            if (uv_temp)
                free(uv_temp);
        }
        else if (_format == image::FMT_RGB888 && format == image::FMT_GRAYSCALE)
        {
            int height = _width;
            int width = _height;
            uint8_t *src = (uint8_t *)_data;
            uint8_t *dst = (uint8_t *)img->data();
#if CONFIG_OMP_ENABLE
            #pragma omp parallel for
            for (int i = 0; i < width * height; i ++) {
                dst[i] = (src[i * 3 + 0] * 38 + src[i * 3 + 1] * 75 + src[i * 3 + 2] * 15) >> 7;
            }
#else
            for (int i = 0; i < height; i++)
            {
                for (int j = 0; j < width; j++)
                {
                    dst[i * width + j] = (src[(i * width + j) * 3 + 0] * 38 + src[(i * width + j) * 3 + 1] * 75 + src[(i * width + j) * 3 + 2] * 15) >> 7;
                }
            }
#endif
        }
        else
        {
            cv::Mat dst(src.rows, src.cols, CV_8UC((int)image::fmt_size[format]), img->data());
            cv::cvtColor(src, dst, cvt_code);
        }

        return img;
    }

    image::Image *Image::to_format(const image::Format &format)
    {
        return to_format(format, nullptr, 0);
    }

    image::Image *Image::to_jpeg(int quality, void *buff, size_t buff_size)
    {
        image::Format format = image::Format::FMT_JPEG;

        cv::Mat src(_height, _width, CV_8UC((int)image::fmt_size[_format]), _data);
#if defined(PLATFORM_MAIXCAM)
        if (quality <= 50)
        {
            quality = 51;
        }
        image::Image *p_img = nullptr;
        image::Image *img = nullptr;
        bool src_alloc = false;
        if (_format == image::FMT_RGB888 || _format == image::FMT_GRAYSCALE || _format == image::FMT_YVU420SP) {
            p_img = this;
        } else {
            p_img = to_format(image::FMT_RGB888);
            src_alloc = true;
        }

        if (!mmf_enc_jpg_push_with_quality(0, (uint8_t *)p_img->data(), _width, _height, mmf_invert_format_to_mmf(p_img->format()), 80))
        {
            uint8_t *data;
            int data_size;
            if (!mmf_enc_jpg_pop(0, &data, &data_size))
            {
                if (buff && (int)buff_size >= data_size) {
                    memcpy(buff, data, data_size);
                    img = new image::Image(_width, _height, format, (uint8_t *)buff, data_size, false);
                } else {
                    img = new image::Image(_width, _height, format, data, data_size, true);
                }
                mmf_enc_jpg_free(0);
            }
        }

        if (src_alloc)
            delete p_img;
        if (!img)
            throw err::Exception(err::ERR_RUNTIME, "convert format failed, see log");
        return img;
#elif defined(PLATFORM_MAIXCAM2)
        image::Image *p_img = nullptr;
        image::Image *img = nullptr;
        maixcam2::Frame *src_frame = nullptr, *out_frame = nullptr;
        bool src_alloc = false;
        if (_format == Format::FMT_YVU420SP || _format == Format::FMT_YUV420SP) {
            p_img = this;
        } else {
            p_img = to_format(image::FMT_YVU420SP);
            src_alloc = true;
        }

        if (maixcam2::ax_jpg_enc_init() != err::ERR_NONE) {
            goto __EXIT;
        }

        src_frame = new maixcam2::Frame(p_img->width(), p_img->height(), p_img->data(), p_img->data_size(), maixcam2::get_ax_fmt_from_maix(p_img->format()));
        if (!src_frame) {
            goto __EXIT;
        }

        out_frame = maixcam2::ax_jpg_enc_once(src_frame, quality);
        if (!out_frame) {
            goto __EXIT;
        }

        if (buff && (int)buff_size >= out_frame->len) {
            memcpy(buff, (uint8_t *)out_frame->data, out_frame->len);
            img = new image::Image(p_img->width(), p_img->height(), format, (uint8_t *)buff, out_frame->len, false);
        } else {
            img = new image::Image(p_img->width(), p_img->height(), format, (uint8_t *)out_frame->data, out_frame->len, true);
        }
__EXIT:
        if (out_frame) {
            delete out_frame;
        }

        if (src_frame) {
            delete src_frame;
        }

        if (src_alloc)
        {
            delete p_img;
        }
        return img;
#else
        // soft convert, slower
        image::Image *p_img = nullptr;
        cv::Mat *p_src = &src;
        bool src_alloc = false;
        if (_format != Format::FMT_BGR888 && _format != Format::FMT_BGRA8888)
        {
            p_img = to_format(image::FMT_BGR888);
            p_src = new cv::Mat(p_img->_height, p_img->_width, CV_8UC((int)image::fmt_size[image::FMT_BGR888]), p_img->_data);
            src_alloc = true;
        }
        cv::InputArray input(*p_src);
        std::vector<uchar> jpeg_buff;
        std::vector<int> params;
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(95);
        cv::imencode(".jpg", input, jpeg_buff, params);
        image::Image *img;
        if (buff)
        {
            if (buff_size < jpeg_buff.size())
            {
                if (src_alloc)
                {
                    delete p_img;
                    delete p_src;
                }
                throw err::Exception(err::ERR_ARGS, "convert format failed, buffer size not enough");
            }
            memcpy(buff, jpeg_buff.data(), jpeg_buff.size());
            img = new image::Image(src.cols, src.rows, format, (uint8_t *)buff, jpeg_buff.size(), false);
        }
        else
            img = new image::Image(src.cols, src.rows, format, (uint8_t *)jpeg_buff.data(), jpeg_buff.size(), true);
        if (src_alloc)
        {
            delete p_img;
            delete p_src;
        }
        return img;
#endif
        return nullptr;
    }

    static cv::Rect _adjustRectToFit(const cv::Rect &rect, const cv::Size &dstSize)
    {
        int newX = std::max(rect.x, 0);
        int newY = std::max(rect.y, 0);
        int newWidth = std::min(rect.width - (newX - rect.x), dstSize.width - newX);
        int newHeight = std::min(rect.height - (newY - rect.y), dstSize.height - newY);
        return cv::Rect(newX, newY, newWidth, newHeight);
    }

    image::Image *Image::draw_image(int x, int y, image::Image &img)
    {
        image::Format fmt = img.format();
        if (!(fmt == image::FMT_GRAYSCALE || fmt == image::FMT_RGB888 || fmt == image::FMT_BGR888 ||
              fmt == image::FMT_RGBA8888 || fmt == image::FMT_BGRA8888))
            throw std::runtime_error("image format not support");
        // check format
        if (image::fmt_size[img.format()] > image::fmt_size[_format])
            throw std::runtime_error("image format not match");
        cv::Mat src(img.height(), img.width(), CV_8UC((int)image::fmt_size[img.format()]), img.data());
        cv::Mat dst(_height, _width, CV_8UC((int)image::fmt_size[_format]), _data);
        cv::Rect rect(x, y, img.width(), img.height());
        cv::Rect adjustedRect = _adjustRectToFit(rect, dst.size());
        int srcX = std::max(0, -x);
        int srcY = std::max(0, -y);
        cv::Rect srcRect(srcX, srcY, adjustedRect.width, adjustedRect.height);
        if (adjustedRect.width <= 0 || adjustedRect.height <= 0)
        {
            throw err::Exception(err::ERR_ARGS, "range error");
        }
        if (_format == fmt && image::fmt_size[fmt] < 4)
        {
            src(srcRect).copyTo(dst(adjustedRect));
        }
        else
        {
            // convert format
            bool img_alloc = false;
            image::Image *img_new = nullptr;
            if (_format != fmt)
            {
                img_new = img.to_format(_format);
                if (!img_new)
                {
                    return img_new;
                }
                img_alloc = true;
            }
            else
                img_new = &img;
            cv::Mat src_new(img_new->height(), img_new->width(), CV_8UC((int)image::fmt_size[_format]), img_new->data());
            if (image::fmt_size[_format] < 4)
            {
                src_new(srcRect).copyTo(dst(adjustedRect));
            }
            else // merge two BGRA images
            {
                uint8_t *data = (uint8_t *)_data;
                #pragma omp parallel for
                for (int y = 0; y < adjustedRect.height; y++)
                {
                    for (int x = 0; x < adjustedRect.width; x++)
                    {
                        cv::Vec4b &pixel = src_new.at<cv::Vec4b>(y + srcY, x + srcX);
                        // ignore transparent pixel
                        if (pixel[3] == 0)
                            continue;
                        // cv::Vec4b &pixel_dst = dst.at<cv::Vec4b>(y + adjustedRect.y, x + adjustedRect.x);
                        // alpha is 255, just copy
                        uint8_t *p = data + (_width * (y + adjustedRect.y) + (x + adjustedRect.x)) * 4;
                        if (pixel[3] == 255)
                        {
                            p[0] = pixel[0];
                            p[1] = pixel[1];
                            p[2] = pixel[2];
                            p[3] = pixel[3];
                        }
                        // alpha > 0 and < 255, blend
                        else
                        {
                            // TODO: optimize blend algorithm
                            p[0] = (uint32_t)((pixel[0] * pixel[3] + p[0] * (255 - pixel[3]))) >> 8;
                            p[1] = (uint32_t)((pixel[1] * pixel[3] + p[1] * (255 - pixel[3]))) >> 8;
                            p[2] = (uint32_t)((pixel[2] * pixel[3] + p[2] * (255 - pixel[3]))) >> 8;
                            p[3] = 255 - (255 - pixel[3]) * (255 - p[3]);
                        }
                    }
                }
            }
            if (img_alloc)
                delete img_new;
        }
        return this;
    }

    image::Image *Image::draw_rect(int x, int y, int w, int h, const image::Color &color, int thickness)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);
        cv::Rect rect(x, y, w, h);
        if (color.alpha != 1 && (_format == Format::FMT_RGBA8888 || _format == Format::FMT_BGRA8888))
        {
            cv::Mat roi = img(rect);
            cv::Mat color_mat(roi.size(), roi.type(), cv_color);
            cv::addWeighted(color_mat, color.alpha, roi, 1 - color.alpha, 0, roi);
        }
        else
            cv::rectangle(img, rect, cv_color, thickness);
        return this;
    }

    image::Image *Image::draw_line(int x1, int y1, int x2, int y2, const image::Color &color, int thickness)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);
        cv::line(img, cv::Point(x1, y1), cv::Point(x2, y2), cv_color, thickness);
        return this;
    }

    image::Image *Image::draw_circle(int x, int y, int radius, const image::Color &color, int thickness)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);
        cv::circle(img, cv::Point(x, y), radius, cv_color, thickness);
        return this;
    }

    image::Image *Image::draw_ellipse(int x, int y, int a, int b, float angle, float start_angle, float end_angle, const image::Color &color, int thickness)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);
        cv::ellipse(img, cv::Point(x, y), cv::Size(a, b), angle, start_angle, end_angle, cv_color, thickness);
        return this;
    }

    image::Image *image::Image::draw_string(int x, int y, const std::string &text, const image::Color &color, float scale, int thickness,
                                            bool wrap, int wrap_space, const std::string &font)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        add_default_fonts(fonts_info);
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);
        cv::Point point(x, y);
        const std::string *final_font = &curr_font_name;
        int final_font_id = curr_font_id;
        // if cont is not empty, use it as font path
        if (!font.empty())
        {
            // if name in fonts_info
            if (fonts_info.find(font) == fonts_info.end())
            {
                log::error("font %d not load\n", font.c_str());
                throw std::runtime_error("font not load");
            }
            final_font = &font;
            final_font_id = get_default_fonts_id(font);
        }
        // auto wrap if text width > image width
        if (!wrap)
        {
            _put_text(img, text, point, cv_color, scale, thickness, *final_font, final_font_id);
        }
        else
        {
            cv::Size size;
            _get_text_size(size, text, *final_font, final_font_id, scale, thickness);
            int text_width = size.width;
            int text_height = size.height;
            int text_max_width = _width - x;
            bool wrap_now = false;
            if (strstr(text.c_str(), "\n") != NULL || strstr(text.c_str(), "\r") != NULL)
                wrap_now = true;

            // auto wrap
            if (wrap_now || text_width > text_max_width)
            {
                // loop to calculate text width, if reach to image width, draw text on image, and move to next line go on
                size_t idx = 0;
                std::string text_tmp;
                cv::Size size_tmp;
                uint8_t char_size = 1;
                bool last_is_r = false;
                wrap_now = false;
                while (idx < text.length())
                {
                    char c = text[idx];
                    wrap_now = false;
                    if (c == '\r')
                    {
                        last_is_r = true;
                        wrap_now = true;
                    }
                    else if (c == '\n')
                    {
                        if (last_is_r)
                        {
                            last_is_r = false;
                            ++idx;
                            continue;
                        }
                        wrap_now = true;
                        last_is_r = false;
                    }
                    else
                    {
                        last_is_r = false;
                    }
                    if (wrap_now)
                    {
                        _put_text(img, text_tmp, point, cv_color, scale, thickness, *final_font, final_font_id);
                        point.x = x;
                        point.y += text_height + wrap_space;
                        text_tmp.clear();
                        if (point.y + text_height >= _height)
                            break;
                        ++idx;
                        continue;
                    }
                    char_size = _get_char_size(c);
                    text_tmp += text.substr(idx, char_size);
                    cv::Size size_tmp;
                    _get_text_size(size_tmp, text_tmp, *final_font, final_font_id, scale, thickness);
                    if (size_tmp.width >= text_max_width)
                    {
                        if (size_tmp.width > text_max_width)
                        {
                            text_tmp.erase(text_tmp.length() - char_size, char_size);
                        }
                        _put_text(img, text_tmp, point, cv_color, scale, thickness, *final_font, final_font_id);
                        point.x = x;
                        point.y += text_height + wrap_space;
                        text_tmp.clear();
                        if (point.y + text_height >= _height)
                            break;
                        if (size_tmp.width > text_max_width)
                            continue;
                    }
                    idx += char_size;
                }
                // draw last line
                if (!text_tmp.empty())
                {
                    _put_text(img, text_tmp, point, cv_color, scale, thickness, *final_font, final_font_id);
                }
            }
            else
            {
                _put_text(img, text, point, cv_color, scale, thickness, *final_font, final_font_id);
            }
        }
        return this;
    }

    image::Image *Image::draw_cross(int x, int y, const image::Color &color, int size, int thickness)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);
        cv::line(img, cv::Point(x - size, y), cv::Point(x + size, y), cv_color, thickness);
        cv::line(img, cv::Point(x, y - size), cv::Point(x, y + size), cv_color, thickness);
        return this;
    }

    image::Image *Image::draw_arrow(int x0, int y0, int x1, int y1, const image::Color &color, int thickness)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);
        cv::Point start(x0, y0);
        cv::Point end(x1, y1);
        cv::arrowedLine(img, start, end, cv_color, thickness);
        return this;
    }

    image::Image *Image::draw_edges(std::vector<std::vector<int>> corners, const image::Color &color, int size, int thickness, bool fill)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);

        if (corners.size() < 4)
        {
            throw std::runtime_error("corners size must >= 4");
            return nullptr;
        }

        if (fill)
        {
            thickness = -1;
        }

        cv::Point topLeft(corners[0][0], corners[0][1]);
        cv::Point bottomRight(corners[2][0], corners[2][1]);
        cv::rectangle(img, topLeft, bottomRight, cv_color, thickness);
        return this;
    }

    image::Image *Image::draw_keypoints(const std::vector<int> &keypoints, const image::Color &color, int size, int thickness, int line_thickness)
    {
        int ch_format = 0;
        cv::Scalar cv_color;
        _get_cv_format_color(_format, color, &ch_format, cv_color);
        cv::Mat img(_height, _width, ch_format, _data);

        if (keypoints.size() < 2 || keypoints.size() % 2 != 0)
        {
            throw std::runtime_error("keypoints size must >= 2 and multiple of 2");
            return nullptr;
        }
        for (size_t i = 0; i < keypoints.size() / 2; ++i)
        {
            cv::Point center(keypoints[i * 2], keypoints[i * 2 + 1]);
            if (center.x < 0 || center.y < 0)
                continue;
            int radius = size;
            cv::circle(img, center, radius, cv_color, thickness);
        }

        // Draw lines between consecutive keypoints if line_thickness is greater than 0
        if (line_thickness > 0)
        {
            for (size_t i = 1; i < keypoints.size() / 2; ++i)
            {
                cv::Point pt1(keypoints[(i - 1) * 2], keypoints[(i - 1) * 2 + 1]);
                cv::Point pt2(keypoints[i * 2], keypoints[i * 2 + 1]);
                if (pt1.x < 0 || pt1.y < 0 || pt2.x < 0 || pt2.y < 0)
                    continue; // Skip invalid lines with negative coordinates
                cv::line(img, pt1, pt2, cv_color, line_thickness);
            }
            // Draw a line from the last point to the first point to close the loop
            cv::Point pt_first(keypoints[0], keypoints[1]);
            cv::Point pt_last(keypoints[keypoints.size() - 2], keypoints[keypoints.size() - 1]);
            if (pt_first.x >= 0 && pt_first.y >= 0 && pt_last.x >= 0 && pt_last.y >= 0)
            {
                cv::line(img, pt_last, pt_first, cv_color, line_thickness);
            }
        }

        return this;
    }

    image::Image *Image::resize(int width, int height, image::Fit object_fit, image::ResizeMethod method)
    {
        int pixel_num = 0;
        int cv_h = 0;
        int cv_dst_h = 0;
        switch (_format)
        {
        case image::FMT_RGB888:
        case image::FMT_BGR888:
            pixel_num = CV_8UC3;
            cv_h = _height;
            cv_dst_h = height;
            break;
        case image::FMT_GRAYSCALE:
            pixel_num = CV_8UC1;
            cv_h = _height;
            cv_dst_h = height;
            break;
        case image::FMT_BGRA8888:
        case image::FMT_RGBA8888:
            pixel_num = CV_8UC4;
            cv_h = _height;
            cv_dst_h = height;
            break;
        case image::FMT_RGB565:
        case image::FMT_BGR565:
            pixel_num = CV_8UC2;
            cv_h = _height;
            cv_dst_h = height;
            break;
        case image::FMT_YVU420SP:
            pixel_num = CV_8UC1;
            cv_h = _height + _height / 2;
            cv_dst_h = height + height / 2;
            break;
        default:
            throw std::runtime_error("image resize: not support format");
            break;
        }
        /// calculate size if width or height is -1
        if (width == -1)
        {
            width = height * _width / _height;
        }
        else if (height == -1)
        {
            height = width * _height / _width;
        }
        image::Image *ret = new image::Image(width, height, _format);

        cv::Mat img(cv_h, _width, pixel_num, _data);
        cv::Mat dst;
        cv::InterpolationFlags inter_method = (cv::InterpolationFlags)method;
        if (object_fit == image::Fit::FIT_FILL)
        {
            if (_format == image::FMT_YVU420SP)
            {
                cv::Mat rgb, resize_rgb;
                cv::cvtColor(img, rgb, cv::COLOR_YUV2RGB_NV21);
                cv::resize(rgb, resize_rgb, cv::Size(width, height), 0, 0, inter_method);
                dst = cv::Mat(cv_dst_h, width, pixel_num, ret->data());
                _cv_rgb_nv21(resize_rgb, dst, width, height);
            }
            else
            {
                dst = cv::Mat(height, width, pixel_num, ret->data());
                cv::resize(img, dst, cv::Size(width, height), 0, 0, inter_method);
            }
        }
        else if (object_fit == image::Fit::FIT_CONTAIN)
        {
            cv::Mat tmp;
            float scale = std::min((float)width / _width, (float)height / _height);
            cv::resize(img, tmp, cv::Size(), scale, scale, inter_method);
            dst = cv::Mat(cv_dst_h, width, pixel_num, ret->data());
            cv::Rect rect((width - tmp.cols) / 2, (height - tmp.rows) / 2, tmp.cols, tmp.rows);
            tmp.copyTo(dst(rect));
            // fill black color
            if (tmp.cols < width)
            {
                cv::Mat black(height, (width - tmp.cols) / 2, pixel_num, cv::Scalar(0, 0, 0));
                // left black area
                cv::Rect rect(0, 0, (width - tmp.cols) / 2, height);
                black.copyTo(dst(rect));
                // right black area
                rect.x = (width - tmp.cols) / 2 + tmp.cols;
                black.copyTo(dst(rect));
                if (rect.width != width - tmp.cols - rect.width)
                {
                    // one line
                    rect.x = dst.cols - 1;
                    rect.width = 1;
                    cv::Mat black2(height, 1, pixel_num, cv::Scalar(0, 0, 0));
                    black2.copyTo(dst(rect));
                }
            }
            if (tmp.rows < height)
            {
                cv::Mat black((height - tmp.rows) / 2, width, pixel_num, cv::Scalar(0, 0, 0));
                // top black area
                cv::Rect rect(0, 0, width, (height - tmp.rows) / 2);
                black.copyTo(dst(rect));
                // bottom black area
                rect.y = (height - tmp.rows) / 2 + tmp.rows;
                black.copyTo(dst(rect));
                if (rect.height != height - tmp.rows - rect.height)
                {
                    // one line
                    rect.y = dst.rows - 1;
                    rect.height = 1;
                    cv::Mat black2(1, width, pixel_num, cv::Scalar(0, 0, 0));
                    black2.copyTo(dst(rect));
                }
            }
        }
        else if (object_fit == image::Fit::FIT_COVER)
        {
            cv::Mat tmp;
            float scale = std::max((float)width / _width, (float)height / _height);
            cv::resize(img, tmp, cv::Size(), scale, scale, inter_method);
            dst = cv::Mat(cv_dst_h, width, pixel_num, ret->data());
            cv::Rect rect((tmp.cols - width) / 2, (tmp.rows - height) / 2, width, height);
            tmp(rect).copyTo(dst);
        }
        else
        {
            throw std::runtime_error("not support object fit");
        }
        return ret;
    }

    image::Image *Image::affine(std::vector<int> src_points, std::vector<int> dst_points, int width, int height, image::ResizeMethod method)
    {
        if (width < 0 && height < 0)
        {
            throw std::runtime_error("width and height can't both be -1");
        }
        int pixel_num = _get_cv_pixel_num(_format);
        /// calculate size if width or height is -1
        if (width == -1)
        {
            width = height * _width / _height;
        }
        else if (height == -1)
        {
            height = width * _height / _width;
        }
        image::Image *ret = new image::Image(width, height, _format);
        ;
        cv::Mat img(_height, _width, pixel_num, _data);
        cv::Mat dst(height, width, pixel_num, ret->data());
        cv::Point2f srcTri[3];
        cv::Point2f dstTri[3];
        for (int i = 0; i < 3; i++)
        {
            srcTri[i] = cv::Point2f(src_points[i * 2], src_points[i * 2 + 1]);
            dstTri[i] = cv::Point2f(dst_points[i * 2], dst_points[i * 2 + 1]);
        }
        cv::Mat warp_mat = cv::getAffineTransform(srcTri, dstTri);
        cv::warpAffine(img, dst, warp_mat, dst.size(), (cv::InterpolationFlags)method);
        return ret;
    }

    image::Image *Image::perspective(std::vector<int> src_points, std::vector<int> dst_points, int width, int height, image::ResizeMethod method)
    {
        if (width < 0 && height < 0)
        {
            throw std::runtime_error("width and height can't both be -1");
        }
        if (src_points.size() < 8 || dst_points.size() < 8)
        {
            throw std::invalid_argument("4 points are required for perspective transform.");
        }

        int pixel_num = _get_cv_pixel_num(_format);

        // Calculate size if width or height is -1
        if (width == -1)
        {
            width = height * _width / _height;
        }
        else if (height == -1)
        {
            height = width * _height / _width;
        }

        // Create output image
        image::Image *ret = new image::Image(width, height, _format);

        // Convert source and destination points to cv::Point2f
        std::vector<cv::Point2f> srcPts, dstPts;
        for (size_t i = 0; i < 8; i += 2)
        {
            srcPts.push_back(cv::Point2f(src_points[i], src_points[i + 1]));
            dstPts.push_back(cv::Point2f(dst_points[i], dst_points[i + 1]));
        }

        // Calculate perspective transformation matrix
        cv::Mat warp_mat = cv::getPerspectiveTransform(srcPts, dstPts);

        // Apply the perspective transform to the image
        cv::Mat img(_height, _width, pixel_num, _data);
        cv::Mat dst(height, width, pixel_num, ret->data());

        cv::warpPerspective(img, dst, warp_mat, dst.size(), (cv::InterpolationFlags)method);

        return ret;
    }

    image::Image *Image::copy()
    {
        image::Image *ret = new image::Image(_width, _height, _format);

#if defined(PLATFORM_MAIXCAM2)
        auto src = (uint8_t *)_data;
        auto dst = (uint8_t *)ret->data();
        auto copy_size = ret->data_size() / 2;
#pragma omp parallel sections
{
#pragma omp section
        memcpy(dst, src, copy_size);
#pragma omp section
        memcpy(dst + copy_size, src + copy_size, copy_size);
}
#else
        memcpy(ret->data(), _data, _width * _height * image::fmt_size[_format]);
#endif
        return ret;
    }

    image::Image *Image::crop(int x, int y, int w, int h)
    {
        image::Image *ret = new image::Image(w, h, _format);
        ;
        int pixel_num = _get_cv_pixel_num(_format);
        cv::Mat img(_height, _width, pixel_num, _data);
        cv::Mat dst(h, w, pixel_num, ret->data());
        cv::Rect rect(x, y, w, h);
        img(rect).copyTo(dst);
        return ret;
    }

    image::Image *Image::rotate(float angle, int width, int height, image::ResizeMethod method)
    {
        int pixel_num = _get_cv_pixel_num(_format);
        if (width < 0 && height < 0)
        {
            if (angle == 90 || angle == 270)
            {
                width = _height;
                height = _width;
            }
        }
        /// calculate size if width or height is -1
        if (width < 0)
        {
            double radians = angle * M_PI / 180.0;
            width = _width * fabs(cos(radians)) + _height * fabs(sin(radians));
        }
        if (height < 0)
        {
            double radians = angle * M_PI / 180.0;
            height = _width * fabs(sin(radians)) + _height * fabs(cos(radians));
        }
        image::Image *ret = new image::Image(width, height, _format);
        ;
        cv::Mat img(_height, _width, pixel_num, _data);
        cv::Mat dst(height, width, pixel_num, ret->data());
        cv::Point2f center((float)_width / 2.0, (float)_height / 2.0);
        cv::Mat rot_mat = cv::getRotationMatrix2D(center, angle, 1.0);
        // M[0, 2] += (width - w) / 2
        // M[1, 2] += (height - h) / 2
        rot_mat.at<double>(0, 2) += (width - _width) / 2.0;
        rot_mat.at<double>(1, 2) += (height - _height) / 2.0;
        cv::warpAffine(img, dst, rot_mat, dst.size(), (cv::InterpolationFlags)method);
        return ret;
    }

    Image *Image::flip(const FlipDir dir)
    {
        int pixel_num = _get_cv_pixel_num(_format);
        Image *ret = new Image(_width, _height, _format);
        cv::Mat img(_height, _width, pixel_num, _data);
        cv::Mat dst(_height, _width, pixel_num, ret->data());

        int flipCode;
        switch (dir)
        {
        case FlipDir::X:
            flipCode = 0; // 垂直翻转
            break;
        case FlipDir::Y:
            flipCode = 1; // 水平翻转
            break;
        case FlipDir::XY:
            flipCode = -1; // 同时进行垂直和水平翻转
            break;
        default:
            throw err::Exception(err::ERR_ARGS);
        }
        cv::flip(img, dst, flipCode);
        return ret;
    }

    err::Err Image::save(const char *path, int quality)
    {
        if (_height <= 0 || _width <= 0)
        {
            log::error("save image failed, image size is invalid\n");
            return err::ERR_ARGS;
        }
        cv::Mat img(_height, _width, CV_8UC((int)image::fmt_size[_format]), _data);
        std::vector<int> params;
        if (quality >= 0 && quality <= 100)
        {
            params.push_back(cv::IMWRITE_JPEG_QUALITY);
            params.push_back(quality);
        }
        else if (quality >= -1 && quality <= 9)
        {
            params.push_back(cv::IMWRITE_PNG_COMPRESSION);
            params.push_back(quality);
        }
        else
        {
            return err::ERR_ARGS;
        }
        log::debug("save image to %s\n", path);
        // not not dir exists, create it
        std::string dir = path;
        size_t pos = dir.find_last_of('/');
        if (pos != std::string::npos)
        {
            dir = dir.substr(0, pos);
            if (!fs::exists(dir))
            {
                if (fs::mkdir(dir) < 0)
                {
                    log::error("create dir %s failed\n", dir.c_str());
                    return err::ERR_IO;
                }
            }
        }
        bool ok = false;
        switch (_format)
        {
        case FMT_BGR888:
        case FMT_BGRA8888:
            ok = cv::imwrite(path, img, params);
            if (!ok)
                return err::ERR_RUNTIME;
            break;
        case FMT_RGB888:
            cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
            ok = cv::imwrite(path, img, params);
            cv::cvtColor(img, img, cv::COLOR_RGB2BGR);
            if (!ok)
                return err::ERR_RUNTIME;
            break;
        case FMT_RGBA8888:
            cv::cvtColor(img, img, cv::COLOR_RGBA2BGRA);
            ok = cv::imwrite(path, img, params);
            cv::cvtColor(img, img, cv::COLOR_RGBA2BGRA);
            if (!ok)
                return err::ERR_RUNTIME;
            break;
        case FMT_YVU420SP:
        {
            cv::Mat bgr;
            img.rows = _height * 3 / 2;
            cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_NV21);
            ok = cv::imwrite(path, bgr, params);
            if (!ok)
                return err::ERR_RUNTIME;
            break;
        }
        case FMT_YUV420SP:
        {
            cv::Mat bgr;
            img.rows = _height * 3 / 2;
            cv::cvtColor(img, bgr, cv::COLOR_YUV2BGR_NV12);
            ok = cv::imwrite(path, bgr, params);
            if (!ok)
                return err::ERR_RUNTIME;
            break;
        }
        case FMT_JPEG:
        {
            std::string lower_path(path);
            std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });
            if (path_is_format(path, ".jpeg") || path_is_format(path, ".jpg"))
            {
                fs::File *f = fs::open(path, "wb");
                if (!f)
                    return err::ERR_IO;
                f->write(_data, _data_size);
                f->close();
                delete f;
            }
            else
            {
                Image *tmp = to_format(FMT_BGR888);
                if (!tmp)
                    return err::ERR_ARGS;
                cv::Mat img2(_height, _width, CV_8UC3, tmp->data());
                ok = cv::imwrite(path, img2, params);
                delete tmp;
                if (!ok)
                    return err::ERR_RUNTIME;
            }
            break;
        }
        case FMT_PNG:
        {
            if (path_is_format(path, ".png"))
            {
                fs::File *f = fs::open(path, "wb");
                if (!f)
                    return err::ERR_IO;
                f->write(_data, _data_size);
                f->close();
                delete f;
            }
            else
            {
                Image *tmp = to_format(FMT_BGR888);
                if (!tmp)
                    return err::ERR_ARGS;
                cv::Mat img2(_height, _width, CV_8UC3, tmp->data());
                ok = cv::imwrite(path, img2, params);
                delete tmp;
                if (!ok)
                    return err::ERR_RUNTIME;
            }
            break;
        }
        default:
            log::error("format %s not support", fmt_names[_format].c_str());
            return err::ERR_NOT_IMPL;
            break;
        }
        return err::ERR_NONE;
    }

    image::Size string_size(std::string text, float scale, int thickness, const std::string &font)
    {
        const std::string *final_font = &curr_font_name;
        int final_font_id = curr_font_id;
        // if cont is not empty, use it as font path
        if (!font.empty())
        {
            // if name in fonts_info
            if (fonts_info.find(font) == fonts_info.end())
            {
                log::error("font %d not load\n", font.c_str());
                throw std::runtime_error("font not load");
            }
            final_font = &font;
            final_font_id = get_default_fonts_id(font);
        }
        cv::Size size;
        _get_text_size(size, text, *final_font, final_font_id, scale, thickness);
        return Size(size.width, size.height);
    }

    err::Err image2cv(image::Image &img, cv::Mat &mat, bool ensure_bgr, bool copy)
    {
        int width = img.width();
        int height = img.height();
        int channels = img.shape()[2];
        uint8_t *img_data = (uint8_t *)img.data();

        if (width <= 0 || height <= 0 || channels <= 0 || img_data == nullptr)
        {
            log::error("image2cv arg error");
            return err::Err::ERR_ARGS;
        }

        if (channels == 1)
        {
            if (copy)
            {
                mat = cv::Mat(height, width, CV_8UC1);
                if(ensure_bgr)
                {
                    cv::cvtColor(mat, mat, cv::COLOR_GRAY2BGR);
                }
                else
                {
                    memcpy(mat.data, img_data, width * height);
                }
            }
            else
            {
                mat = cv::Mat(height, width, CV_8UC1, (void *)img_data);
            }
        }
        else if (channels == 3)
        {
            if (copy)
            {
                mat = cv::Mat(height, width, CV_8UC3);
                memcpy(mat.data, img_data, width * height * 3);
                if(ensure_bgr && img.format() == image::FMT_RGB888)
                {
                    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
                }
            }
            else
            {
                mat = cv::Mat(height, width, CV_8UC3, (void *)img_data);
            }
        }
        else if (channels == 4)
        {
            if (copy)
            {
                mat = cv::Mat(height, width, CV_8UC4);
                memcpy(mat.data, img_data, width * height * 4);
                if(ensure_bgr && img.format() == image::FMT_RGBA8888)
                {
                    cv::cvtColor(mat, mat, cv::COLOR_RGBA2BGRA);
                }
            }
            else
            {
                mat = cv::Mat(height, width, CV_8UC4, (void *)img_data);
            }
        }
        else
        {
            log::error("not support channel num %d", channels);
            return err::Err::ERR_ARGS;
        }
        return err::ERR_NONE;
    }

    image::Image *cv2image(cv::Mat &mat, bool bgr = true, bool copy = true)
    {
        int width = mat.cols;
        int height = mat.rows;
        int channels = mat.channels();
        image::Format format = image::FMT_RGB888;
        if (channels == 1)
        {
            format = image::FMT_GRAYSCALE;
        }
        else if (channels == 3)
        {
            format = bgr ? image::FMT_BGR888 : image::FMT_RGB888;
        }
        else if (channels == 4)
        {
            format = bgr ? image::FMT_BGRA8888 : image::FMT_RGBA8888;
        }
        else
        {
            log::error("not support channel num %d", channels);
            return nullptr;
        }
        return new image::Image(width, height, format, mat.data, width * height * channels, copy);
    }

    std::vector<int> resize_map_pos(int w_in, int h_in, int w_out, int h_out, image::Fit fit, int x, int y, int w, int h)
    {
        float scale_x = static_cast<float>(w_out) / w_in;
        float scale_y = static_cast<float>(h_out) / h_in;
        std::vector<int> result;

        if (fit == image::Fit::FIT_FILL)
        {
            x = static_cast<int>(x * scale_x);
            y = static_cast<int>(y * scale_y);
            if (w > 0)
            {
                w = static_cast<int>(w * scale_x);
            }
            if (h > 0)
            {
                h = static_cast<int>(h * scale_y);
            }
        }
        else if (fit == image::Fit::FIT_CONTAIN)
        {
            float scale = std::min(scale_x, scale_y);
            if (w_out > h_out)
            {
                x = static_cast<int>((w_out - w_in * scale) / 2 + x * scale);
                y = static_cast<int>(y * scale);
            }
            else
            {
                x = static_cast<int>(x * scale);
                y = static_cast<int>((h_out - h_in * scale) / 2 + y * scale);
            }
            if (w > 0)
            {
                w = static_cast<int>(w * scale);
                h = static_cast<int>(h * scale);
            }
        }
        else if (fit == image::Fit::FIT_COVER)
        {
            float scale = std::max(scale_x, scale_y);
            if (w_out > h_out)
            {
                x = static_cast<int>(x * scale);
                y = static_cast<int>((h_out - h_in * scale) / 2 + y * scale);
            }
            else
            {
                x = static_cast<int>((w_out - w_in * scale) / 2 + x * scale);
                y = static_cast<int>(y * scale);
            }
            if (w > 0)
            {
                w = static_cast<int>(w * scale);
                h = static_cast<int>(h * scale);
            }
        }
        else
        {
            throw err::Exception(err::ERR_ARGS, "Unsupported fit mode");
        }

        // Populate the result vector
        result.push_back(x);
        result.push_back(y);
        if (w > 0 && h > 0)
        {
            result.push_back(w);
            result.push_back(h);
        }
        return result;
    }

    std::vector<int> resize_map_pos_reverse(int w_in, int h_in, int w_out, int h_out, image::Fit fit, int x, int y, int w, int h)
    {
        float scale_x = static_cast<float>(w_out) / w_in;
        float scale_y = static_cast<float>(h_out) / h_in;
        std::vector<int> result;

        if (fit == image::Fit::FIT_FILL)
        {
            x = static_cast<int>(x / scale_x);
            y = static_cast<int>(y / scale_y);
            if (w > 0)
            {
                w = static_cast<int>(w / scale_x);
            }
            if (h > 0)
            {
                h = static_cast<int>(h / scale_y);
            }
        }
        else if (fit == image::Fit::FIT_CONTAIN)
        {
            float scale = std::min(scale_x, scale_y);
            if (w_out > h_out)
            {
                x = static_cast<int>((x - (w_out - w_in * scale) / 2) / scale);
                y = static_cast<int>(y / scale);
            }
            else
            {
                x = static_cast<int>(x / scale);
                y = static_cast<int>((y - (h_out - h_in * scale) / 2) / scale);
            }
            if (w > 0)
            {
                w = static_cast<int>(w / scale);
                h = static_cast<int>(h / scale);
            }
        }
        else if (fit == image::Fit::FIT_COVER)
        {
            float scale = std::max(scale_x, scale_y);
            if (w_out > h_out)
            {
                x = static_cast<int>(x / scale);
                y = static_cast<int>((y - (h_out - h_in * scale) / 2) / scale);
            }
            else
            {
                x = static_cast<int>((x - (w_out - w_in * scale) / 2) / scale);
                y = static_cast<int>(y * scale);
            }
            if (w > 0)
            {
                w = static_cast<int>(w / scale);
                h = static_cast<int>(h / scale);
            }
        }
        else
        {
            throw err::Exception(err::ERR_ARGS, "Unsupported fit mode");
        }

        // Populate the result vector
        result.push_back(x);
        result.push_back(y);
        if (w > 0 && h > 0)
        {
            result.push_back(w);
            result.push_back(h);
        }
        return result;
    }

    void _get_cv_format_color(image::Format _format, const image::Color &color_in, int *ch_format, cv::Scalar &cv_color)
    {
        bool color_alloc = false;
        image::Color *color = (image::Color *)&color_in;
        if (color->format != _format)
        {
            color = color->to_format2(_format);
            color_alloc = true;
        }
        switch (_format)
        {
        case image::FMT_RGB888:
            *ch_format = CV_8UC3;
            cv_color = cv::Scalar(color->r, color->g, color->b);
            break;
        case image::FMT_BGR888:
            *ch_format = CV_8UC3;
            cv_color = cv::Scalar(color->b, color->g, color->r);
            break;
        case image::FMT_GRAYSCALE:
            *ch_format = CV_8UC1;
            cv_color = cv::Scalar(color->gray);
            break;
        case image::FMT_BGRA8888:
            *ch_format = CV_8UC4;
            cv_color = cv::Scalar(color->b, color->g, color->r, color->alpha * 255);
            break;
        case image::FMT_RGBA8888:
            *ch_format = CV_8UC4;
            cv_color = cv::Scalar(color->r, color->g, color->b, color->alpha * 255);
            break;
        default:
            throw std::runtime_error("not support format");
            break;
        }
        if (color_alloc)
            delete color;
    }

    int Image::_get_cv_pixel_num(image::Format &format)
    {
        int pixel_num = 0;
        switch (format)
        {
        case image::FMT_RGB888:
        case image::FMT_BGR888:
            pixel_num = CV_8UC3;
            break;
        case image::FMT_GRAYSCALE:
            pixel_num = CV_8UC1;
            break;
        case image::FMT_BGRA8888:
        case image::FMT_RGBA8888:
            pixel_num = CV_8UC4;
            break;
        case image::FMT_RGB565:
        case image::FMT_BGR565:
            pixel_num = CV_8UC2;
            break;
        default:
            throw std::runtime_error("not support format");
            break;
        }
        return pixel_num;
    }

    std::vector<int> Image::_get_available_roi(std::vector<int> roi, std::vector<int> other_roi)
    {
        int src_x, src_y, src_w, src_h;

        if (other_roi.size() == 0)
        {
            src_x = 0;
            src_y = 0;
            src_w = _width;
            src_h = _height;
        }
        else if (other_roi.size() == 4)
        {
            src_x = other_roi[0];
            src_y = other_roi[1];
            src_w = other_roi[2];
            src_h = other_roi[3];
        }
        else
        {
            throw err::Exception("other_roi size must be 4");
        }

        if (roi.size() == 0)
        {
            return std::vector<int>{0, 0, _width, _height};
        }
        else if (roi.size() != 4)
        {
            throw err::Exception("roi size must be 4");
        }

        if (roi[2] <= 0 || roi[3] <= 0)
        {
            throw err::Exception("roi width and height must > 0");
        }

        if ((roi[0] >= (src_x + src_w)) || (roi[1] >= (src_y + src_h)) || (roi[0] + roi[2] <= src_x) || (roi[1] + roi[3] <= src_y))
        {
            throw err::Exception("roi does not overlap on the image!");
        }

        int x = std::max(roi[0], src_x);
        int y = std::max(roi[1], src_y);
        int w = std::min(roi[0] + roi[2], src_x + src_w) - x;
        int h = std::min(roi[1] + roi[3], src_y + src_h) - y;
        return std::vector<int>{x, y, w, h};
    }
} // namespace maix::image
