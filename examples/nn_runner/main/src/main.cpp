
#include "maix_basic.hpp"
#include "maix_nn.hpp"
#include "maix_image.hpp"
#include "main.h"
#include <cmath>
#include <algorithm>
#include <functional>

using namespace maix;

// ─────────────────────────────────────────────────────────────────────────────
// Minimal 5×7 bitmap font – pure C++, zero OpenCV calls.
// Each character is 5 bytes: one byte per column (bits 0-6 = rows top→bottom).
// Covers: space, !, 0-9, A-Z, a-z, %.
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' ' 0x20
    {0x00,0x00,0x5f,0x00,0x00}, // '!' 0x21
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7f,0x14,0x7f,0x14}, // '#'
    {0x24,0x2a,0x7f,0x2a,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%' 0x25
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '\''
    {0x00,0x1c,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1c,0x00}, // ')'
    {0x14,0x08,0x3e,0x08,0x14}, // '*'
    {0x08,0x08,0x3e,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    // 0-9
    {0x3e,0x51,0x49,0x45,0x3e}, // '0'
    {0x00,0x42,0x7f,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4b,0x31}, // '3'
    {0x18,0x14,0x12,0x7f,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3c,0x4a,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1e}, // '9'
    // ':' ';' '<' '=' '>' '?' '@'
    {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},
    {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},
    {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3e},
    // A-Z
    {0x7e,0x11,0x11,0x11,0x7e}, // A
    {0x7f,0x49,0x49,0x49,0x36}, // B
    {0x3e,0x41,0x41,0x41,0x22}, // C
    {0x7f,0x41,0x41,0x22,0x1c}, // D
    {0x7f,0x49,0x49,0x49,0x41}, // E
    {0x7f,0x09,0x09,0x09,0x01}, // F
    {0x3e,0x41,0x49,0x49,0x7a}, // G
    {0x7f,0x08,0x08,0x08,0x7f}, // H
    {0x00,0x41,0x7f,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3f,0x01}, // J
    {0x7f,0x08,0x14,0x22,0x41}, // K
    {0x7f,0x40,0x40,0x40,0x40}, // L
    {0x7f,0x02,0x0c,0x02,0x7f}, // M
    {0x7f,0x04,0x08,0x10,0x7f}, // N
    {0x3e,0x41,0x41,0x41,0x3e}, // O
    {0x7f,0x09,0x09,0x09,0x06}, // P
    {0x3e,0x41,0x51,0x21,0x5e}, // Q
    {0x7f,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7f,0x01,0x01}, // T
    {0x3f,0x40,0x40,0x40,0x3f}, // U
    {0x1f,0x20,0x40,0x20,0x1f}, // V
    {0x3f,0x40,0x38,0x40,0x3f}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    // '[' '\' ']' '^' '_' '`'
    {0x00,0x7f,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},
    {0x00,0x41,0x41,0x7f,0x00},
    {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},
    // a-z
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7f,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7f}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7e,0x09,0x01,0x02}, // f
    {0x0c,0x52,0x52,0x52,0x3e}, // g
    {0x7f,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7d,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3d,0x00}, // j
    {0x7f,0x10,0x28,0x44,0x00}, // k
    {0x00,0x41,0x7f,0x40,0x00}, // l
    {0x7c,0x04,0x18,0x04,0x78}, // m
    {0x7c,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7c,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7c}, // q
    {0x7c,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3f,0x44,0x40,0x20}, // t
    {0x3c,0x40,0x40,0x40,0x3c}, // u
    {0x1c,0x20,0x40,0x20,0x1c}, // v
    {0x3c,0x40,0x30,0x40,0x3c}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0c,0x50,0x50,0x50,0x3c}, // y
    {0x44,0x64,0x54,0x4c,0x44}, // z
};
static_assert(sizeof(FONT5X7)/sizeof(FONT5X7[0]) == 0x7b - 0x20,
              "font table size mismatch");

// Draw one character at pixel (px,py) with given scale (integer, ≥1).
static void _raw_char(image::Image *im, int px, int py,
                      char c, uint8_t r, uint8_t g, uint8_t b, int scale = 2)
{
    int idx = (unsigned char)c - 0x20;
    if (idx < 0 || idx >= (int)(sizeof(FONT5X7)/sizeof(FONT5X7[0]))) return;
    const uint8_t *col = FONT5X7[idx];
    int W = im->width(), H = im->height();
    uint8_t *base = static_cast<uint8_t *>(im->data());
    for (int cx = 0; cx < 5; cx++) {
        for (int row = 0; row < 7; row++) {
            if (!(col[cx] & (1 << row))) continue;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int fx = px + cx * scale + sx;
                    int fy = py + row * scale + sy;
                    if (fx < 0 || fx >= W || fy < 0 || fy >= H) continue;
                    uint8_t *p = base + (fy * W + fx) * 3;
                    p[0] = r; p[1] = g; p[2] = b;
                }
            }
        }
    }
}

// Draw a null-terminated string.
// scale=2 → each pixel becomes 2×2 → effective char size 10×14.
static void _raw_text(image::Image *im, int px, int py,
                      const char *s, uint8_t r, uint8_t g, uint8_t b, int scale = 2)
{
    while (*s) {
        _raw_char(im, px, py, *s, r, g, b, scale);
        px += (5 + 1) * scale;   // 5 px wide + 1 px spacing
        s++;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw pixel helpers – bypass OpenCV drawing entirely.
// cv::rectangle and cv::line with thickness>1 crash on OpenCV 3.2 / RISC-V
// because the thick-line SIMD path sign-extends 32-bit row coordinates into
// 64-bit pointers (produces address 0xfffffff400000000 → SIGSEGV).
// ─────────────────────────────────────────────────────────────────────────────
static void _raw_hline(image::Image *im,
                       int x0, int x1, int y,
                       uint8_t r, uint8_t g, uint8_t b)
{
    int W = im->width(), H = im->height();
    if (y < 0 || y >= H) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(W - 1, x1);
    if (x0 > x1) return;
    uint8_t *row = static_cast<uint8_t *>(im->data()) + y * W * 3;
    for (int x = x0; x <= x1; x++) {
        row[x * 3 + 0] = r;
        row[x * 3 + 1] = g;
        row[x * 3 + 2] = b;
    }
}

static void _raw_vline(image::Image *im,
                       int x, int y0, int y1,
                       uint8_t r, uint8_t g, uint8_t b)
{
    int W = im->width(), H = im->height();
    if (x < 0 || x >= W) return;
    if (y0 > y1) std::swap(y0, y1);
    y0 = std::max(0, y0);
    y1 = std::min(H - 1, y1);
    for (int y = y0; y <= y1; y++) {
        uint8_t *p = static_cast<uint8_t *>(im->data()) + (y * W + x) * 3;
        p[0] = r; p[1] = g; p[2] = b;
    }
}

// Draw a hollow rectangle border with `thick` pixel thickness
static void _raw_rect_border(image::Image *im,
                              int x, int y, int w, int h,
                              uint8_t r, uint8_t g, uint8_t b, int thick)
{
    for (int t = 0; t < thick; t++) {
        _raw_hline(im, x,       x+w-1, y+t,       r, g, b); // top
        _raw_hline(im, x,       x+w-1, y+h-1-t,   r, g, b); // bottom
        _raw_vline(im, x+t,     y,     y+h-1,      r, g, b); // left
        _raw_vline(im, x+w-1-t, y,     y+h-1,      r, g, b); // right
    }
}

// Draw a filled rectangle
static void _raw_filled_rect(image::Image *im,
                              int x, int y, int w, int h,
                              uint8_t r, uint8_t g, uint8_t b)
{
    for (int row = y; row < y + h; row++)
        _raw_hline(im, x, x + w - 1, row, r, g, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// COCO 80 class labels (YOLOv5 default)
// ─────────────────────────────────────────────────────────────────────────────
static const std::vector<std::string> COCO_LABELS = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck","boat",
    "traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat",
    "dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack",
    "umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball",
    "kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket",
    "bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple",
    "sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair",
    "couch","potted plant","bed","dining table","toilet","tv","laptop","mouse",
    "remote","keyboard","cell phone","microwave","oven","toaster","sink",
    "refrigerator","book","clock","vase","scissors","teddy bear","hair drier",
    "toothbrush"
};

// ─────────────────────────────────────────────────────────────────────────────
// Detection result
// ─────────────────────────────────────────────────────────────────────────────
struct Detection {
    float x1, y1, x2, y2;   // bbox in original image coordinates
    float conf;
    int   class_id;
};

static inline float _sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

static float _iou(const Detection &a, const Detection &b)
{
    float x1 = std::max(a.x1, b.x1), y1 = std::max(a.y1, b.y1);
    float x2 = std::min(a.x2, b.x2), y2 = std::min(a.y2, b.y2);
    float inter = std::max(0.f, x2 - x1) * std::max(0.f, y2 - y1);
    float aa = (a.x2 - a.x1) * (a.y2 - a.y1);
    float ab = (b.x2 - b.x1) * (b.y2 - b.y1);
    return inter / (aa + ab - inter + 1e-5f);
}

static void _nms(std::vector<Detection> &dets, float nms_thresh)
{
    std::sort(dets.begin(), dets.end(),
              [](const Detection &a, const Detection &b){ return a.conf > b.conf; });
    std::vector<bool> removed(dets.size(), false);
    for (size_t i = 0; i < dets.size(); i++) {
        if (removed[i]) continue;
        for (size_t j = i + 1; j < dets.size(); j++)
            if (!removed[j] && dets[i].class_id == dets[j].class_id &&
                _iou(dets[i], dets[j]) > nms_thresh)
                removed[j] = true;
    }
    std::vector<Detection> res;
    for (size_t i = 0; i < dets.size(); i++)
        if (!removed[i]) res.push_back(dets[i]);
    dets = res;
}

// ─────────────────────────────────────────────────────────────────────────────
// YOLOv5 post-processing
//   outputs    : 3 output tensors, each [1,255,H,W] in descending stride order
//   img        : original image (for drawing; coordinates are mapped back here)
//   model_w/h  : model input dimensions used during inference
//   conf_thresh: objectness × class score threshold
//   nms_thresh : NMS IoU threshold
//   labels     : class name list
// Returns number of detections; draws results on img and saves result.jpg
// ─────────────────────────────────────────────────────────────────────────────
static int post_process_yolov5(tensor::Tensors *outputs,
                                maix::image::Image *img,
                                int model_w, int model_h,
                                float conf_thresh = 0.30f,
                                float nms_thresh  = 0.45f,
                                const std::vector<std::string> &labels = COCO_LABELS)
{
    // Standard YOLOv5 COCO anchors (P3/8, P4/16, P5/32)
    static const float ANCHORS[3][6] = {
        { 10,  13,  16,  30,  33,  23},   // stride  8
        { 30,  61,  62,  45,  59, 119},   // stride 16
        {116,  90, 156, 198, 373, 326},   // stride 32
    };
    const int NC = 80, NA = 3;

    int img_w = img->width(), img_h = img->height();

    // Compute inverse FIT_COVER transform parameters
    // FIT_COVER scales by max(model_w/img_w, model_h/img_h) and centre-crops.
    float scale_w   = (float)model_w / img_w;
    float scale_h   = (float)model_h / img_h;
    float fit_scale = std::max(scale_w, scale_h);
    float pad_x     = (img_w * fit_scale - model_w) / 2.0f;  // crop offset x
    float pad_y     = (img_h * fit_scale - model_h) / 2.0f;  // crop offset y

    std::vector<Detection> all_dets;

    // ── [diag] output tensor statistics + binary dump ───────────────────────
    log::info("[diag] === YOLOv5 output diagnostics (model_w=%d model_h=%d) ===",
              model_w, model_h);
    {
        int di = 0;
        for (auto &kv : *outputs) {
            if (di >= 3) break;
            const float *d = static_cast<const float *>(kv.second->data());
            auto sh = kv.second->shape();
            if (sh.size() < 4) { di++; continue; }
            int total = 1; for (auto s : sh) total *= s;
            float mn = d[0], mx = d[0], sm = 0.f;
            for (int i = 0; i < total; i++) {
                mn = std::min(mn, d[i]); mx = std::max(mx, d[i]); sm += d[i];
            }
            log::info("[diag]   head[%d] '%s': total=%d range=[%.4f, %.4f] mean=%.5f",
                      di, kv.first.c_str(), total, mn, mx, sm / total);
            // obj channel (ch=4) of anchor-0 only
            int H = sh[2], W = sh[3];
            float obj_mn = 1e9f, obj_mx = -1e9f;
            for (int i = 0; i < H * W; i++) {
                float v = d[4 * H * W + i];
                obj_mn = std::min(obj_mn, v); obj_mx = std::max(obj_mx, v);
            }
            log::info("[diag]     anchor0 obj-raw=[%.4f,%.4f] sigmoid=[%.4f,%.4f]",
                      obj_mn, obj_mx, _sigmoid(obj_mn), _sigmoid(obj_mx));
            // first 16 raw values
            char _buf[256]; int _pos = 0;
            _pos += snprintf(_buf + _pos, sizeof(_buf) - _pos, "[diag]     first16:");
            for (int i = 0; i < std::min(16, total); i++)
                _pos += snprintf(_buf + _pos, sizeof(_buf) - _pos, " %.3f", d[i]);
            log::info("%s", _buf);
            // binary dump
            char path[64]; snprintf(path, sizeof(path), "/tmp/yolo_out%d.bin", di);
            FILE *fp = fopen(path, "wb");
            if (fp) { fwrite(d, sizeof(float), total, fp); fclose(fp);
                      log::info("[diag]     dumped -> %s", path); }
            di++;
        }
    }

    float diag_max_obj = 0.f, diag_max_score = 0.f;

    int head = 0;
    for (auto &kv : *outputs) {
        if (head >= 3) break;
        const float *data = static_cast<const float *>(kv.second->data());
        std::vector<int> shape = kv.second->shape();
        if (shape.size() < 4) { head++; continue; }
        int H = shape[2], W = shape[3];
        float stride = (float)model_w / W;

        for (int a = 0; a < NA; a++) {
            float aw = ANCHORS[head][a * 2];
            float ah = ANCHORS[head][a * 2 + 1];
            int base_a = a * (5 + NC);

            for (int h = 0; h < H; h++) {
                for (int w = 0; w < W; w++) {
                    // channel accessor for NCHW layout
                    auto ch = [&](int c) -> float {
                        return data[(base_a + c) * H * W + h * W + w];
                    };

                    float obj_conf = _sigmoid(ch(4));
                    if (obj_conf > diag_max_obj) diag_max_obj = obj_conf;
                    if (obj_conf < conf_thresh) continue;

                    // find best class
                    float max_cls = 0.f; int max_cls_id = 0;
                    for (int c = 0; c < NC; c++) {
                        float cv = _sigmoid(ch(5 + c));
                        if (cv > max_cls) { max_cls = cv; max_cls_id = c; }
                    }
                    float conf = obj_conf * max_cls;
                    if (conf > diag_max_score) diag_max_score = conf;
                    if (conf < conf_thresh) continue;

                    // decode bbox in model space
                    float cx = (_sigmoid(ch(0)) * 2 - 0.5f + w) * stride;
                    float cy = (_sigmoid(ch(1)) * 2 - 0.5f + h) * stride;
                    float bw = powf(_sigmoid(ch(2)) * 2, 2.f) * aw;
                    float bh = powf(_sigmoid(ch(3)) * 2, 2.f) * ah;

                    // inverse FIT_COVER → original image coordinates
                    float x1 = (cx - bw * 0.5f + pad_x) / fit_scale;
                    float y1 = (cy - bh * 0.5f + pad_y) / fit_scale;
                    float x2 = (cx + bw * 0.5f + pad_x) / fit_scale;
                    float y2 = (cy + bh * 0.5f + pad_y) / fit_scale;

                    x1 = std::max(0.f, std::min(x1, (float)(img_w - 1)));
                    y1 = std::max(0.f, std::min(y1, (float)(img_h - 1)));
                    x2 = std::max(0.f, std::min(x2, (float)(img_w - 1)));
                    y2 = std::max(0.f, std::min(y2, (float)(img_h - 1)));

                    all_dets.push_back({x1, y1, x2, y2, conf, max_cls_id});
                }
            }
        }
        head++;
    }

    _nms(all_dets, nms_thresh);

    log::info("[diag] global max_obj_sigmoid=%.4f  max_combined_score=%.4f  threshold=%.2f",
              diag_max_obj, diag_max_score, conf_thresh);
    log::info("YOLOv5 detected %d object(s) (conf>=%.2f):", (int)all_dets.size(), conf_thresh);

    // Colour palette – cycle through 10 distinct colours per class_id
    static const uint8_t PALETTE[][3] = {
        {255,  56,  56}, {255, 157,  51}, {255, 255,  51}, { 56, 255,  51},
        { 51, 255, 153}, { 51, 255, 255}, { 51, 153, 255}, {153,  51, 255},
        {255,  51, 255}, {255,  51, 153},
    };

    for (auto &d : all_dets) {
        const std::string &label = (d.class_id < (int)labels.size())
                                   ? labels[d.class_id]
                                   : std::to_string(d.class_id);
        log::info("  [%s] conf=%.3f  box=(%.0f,%.0f)-(%.0f,%.0f)",
                  label.c_str(), d.conf, d.x1, d.y1, d.x2, d.y2);

        const uint8_t *pc = PALETTE[d.class_id % 10];
        uint8_t br = pc[0], bg = pc[1], bb = pc[2];

        int rx = (int)d.x1, ry = (int)d.y1;
        int rw = std::max(4, (int)(d.x2 - d.x1));
        int rh = std::max(4, (int)(d.y2 - d.y1));

        // ── bounding box: raw pixel border (no OpenCV) ────────────────────
        _raw_rect_border(img, rx, ry, rw, rh, br, bg, bb, 3);

        // ── label: filled background + white text ─────────────────────────
        std::string txt = label + " " + std::to_string((int)(d.conf * 100 + 0.5f)) + "%";
        const int font_h = 16;
        const int pad    =  2;
        int lw = (int)(txt.size() * font_h * 0.55f + pad * 2 + 4);
        int lh = font_h + pad * 2;
        int lx = std::max(0, std::min(rx, img->width()  - lw));
        int ly = std::max(0, ry - lh);

        // raw filled rectangle for label background (no OpenCV)
        _raw_filled_rect(img, lx, ly, lw, lh, br, bg, bb);

        // bitmap text – no OpenCV drawing at all
        _raw_text(img, lx + pad, ly + pad, txt.c_str(), 255, 255, 255, 2);
    }

    return (int)all_dets.size();
}

int post_process_classifier(tensor::Tensors *outputs, const std::string &label_path)
{
    int ret = 0;

    // only support one output
    if((*outputs).size() != 1)
    {
        log::error("only support one output model for classifier");
        return -1;
    }

    // load labels
    std::vector<std::string> labels;
    fs::File *f = fs::open(label_path, "r");
    if(!f)
    {
        log::error("open label file %s failed", label_path);
        return -1;
    }
    std::string line;
    while(f->readline(line) > 0)
    {
        // strip line
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        labels.push_back(line);
    }
    f->close();
    delete f;

    // get top 5 max probability and print
    tensor::Tensor *tensor = outputs->begin()->second;
    std::tuple<tensor::Tensor*, std::vector<int>*> topk = tensor->topk(5);
    log::info("total labels num: %d", labels.size());
    log::info("top 5 result:");
    tensor::Tensor *topk_tensor = std::get<0>(topk);
    std::vector<int> *topk_index = std::get<1>(topk);
    for(size_t i = 0; i < topk_index->size(); i++)
    {
        int index = (*topk_index)[i];
        float value = *((float*)topk_tensor->data() + i);
        log::info("  %d: '%s', %.2f", i, labels[index].c_str(), value);
    }
    printf("\n");
    delete topk_index;
    delete topk_tensor;

    return ret;
}

static std::vector<std::string> split(const std::string &s, const std::string &delimiter) {
    std::vector<std::string> tokens;
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;

    while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        tokens.push_back (token);
    }

    tokens.push_back (s.substr (pos_start));
    return tokens;
}

int _main(int argc, char* argv[])
{
    uint64_t t = time::ticks_ms();
    log::info("Program start");
    std::string model_type = "unknown";
    int ret = 0;
    maix::image::Format img_fmt = maix::image::FMT_RGB888;
    std::vector<float> mean = {};
    std::vector<float> scale = {};
    int forward_loop = 1;

    std::string help = "Usage: " + std::string(argv[0]) + " mud_model_path image_path";

    // model path from argv[1], image from argv[2]
    if(argc < 3)
    {
        log::error("model path not set, %s", help.c_str());
        return -1;
    }
    const char *model_path = argv[1];
    const char *img_path = argv[2];
    // have argv[3], is forward loop test
    if(argc >= 4)
    {
        forward_loop = std::stoi(argv[3]);
        log::info("forward loop times: %d", forward_loop);
    }
    string label_path = "";

    nn::NN model(model_path);
    log::info("load model %s success, time: %d ms", model_path, time::ticks_ms() - t);

    // get model info like preprocess, postprocess info, etc.
    std::map<std::string, std::string> extra_info = model.extra_info();
    if(extra_info.find("model_type") != extra_info.end())
    {
        model_type = extra_info["model_type"];
    }
    if(extra_info.find("input_type") != extra_info.end())
    {
        std::string input_type = extra_info["input_type"];
        if(input_type == "rgb")
        {
            img_fmt = maix::image::FMT_RGB888;
        }
        else if(input_type == "bgr")
        {
            img_fmt = maix::image::FMT_BGR888;
        }
        else
        {
            log::error("unknown input type: %s", input_type.c_str());
            return -1;
        }
    }
    if(extra_info.find("mean") != extra_info.end())
    {
        std::string mean_str = extra_info["mean"];
        std::vector<std::string> mean_strs = split(mean_str, ",");
        for(auto &it : mean_strs)
        {
            mean.push_back(std::stof(it));
        }
    }
    if(extra_info.find("scale") != extra_info.end())
    {
        std::string scale_str = extra_info["scale"];
        std::vector<std::string> scale_strs = split(scale_str, ",");
        for(auto &it : scale_strs)
        {
            scale.push_back(std::stof(it));
        }
    }
    if(extra_info.find("labels") != extra_info.end())
    {
        label_path = fs::dirname(model_path) + "/" + extra_info["labels"];
        log::info("label path: %s", label_path.c_str());
    }

    // printf args info
    log::print("\n");
    log::info("model path: %s", model_path);
    log::info("image path: %s", img_path);
    log::info("model type: %s", model_type.c_str());
    log::info("image format: %s", img_fmt == maix::image::FMT_RGB888 ? "rgb" : "bgr");
    log::info("mean: ");
    for(auto &it : mean)
    {
        log::print("%f, ", it);
    }
    log::print("\n");
    log::info("scale: ");
    for(auto &it : scale)
    {
        log::print("%f, ", it);
    }
    log::print("\n");

    std::vector<nn::LayerInfo> inputs_info = model.inputs_info();
    for(auto &it : inputs_info)
    {
        log::info("input '%s': %s", it.name.c_str(), it.to_str().c_str());
    }

    std::vector<nn::LayerInfo> outputs_info = model.outputs_info();
    for(auto &it : outputs_info)
    {
        log::info("output '%s': %s", it.name.c_str(), it.to_str().c_str());
    }

    log::info("load image now");
    t = time::ticks_ms();
    maix::image::Image *img = maix::image::load(img_path, img_fmt);
    if(!img)
    {
        log::error("load image %s failed", img_path);
        return -1;
    }
    log::info("load image %s success: %s, time: %d ms", img_path, img->to_str().c_str(), time::ticks_ms() - t);

    if(img->width() != inputs_info[0].shape[3] || img->height() != inputs_info[0].shape[2])
    {
        log::warn("image size not match model input size, will auto resize from %dx%d to %dx%d", img->width(), img->height(), inputs_info[0].shape[3], inputs_info[0].shape[2]);
    }
    t = time::ticks_us();
    tensor::Tensors *outputs;
    int count = 0;
    while (1)
    {
        outputs = model.forward_image(*img, mean, scale, maix::image::FIT_COVER);
        if(!outputs)
        {
            log::error("forward image failed");
            goto end;
        }
        ++ count;
        if(count >= forward_loop)
            break;
        delete outputs;
    }
    if(outputs)
    {
        log::info("forward image success, time: %d us", (int)((time::ticks_us() - t) / forward_loop));
        for(auto &it : *outputs)
        {
            tensor::Tensor *tensor = it.second;
            log::info("output '%s': %s", it.first.c_str(), (*tensor).to_str().c_str());
        }
        // post process
        if(model_type == "classifier")
        {
            log::info("post process for classifier model");
            if(label_path.empty())
            {
                log::error("label path not set");
                goto end;
            }
            ret = post_process_classifier(outputs, label_path);
        }
        else if(model_type == "yolov5" ||
                (model_type == "unknown" && [&]() -> bool {
                    // auto-detect: 3 outputs each shaped [1, 255, H, W]
                    if(outputs->size() != 3) return false;
                    for(auto &it : *outputs) {
                        auto s = it.second->shape();
                        if(s.size() != 4 || s[1] != 255) return false;
                    }
                    return true;
                }()))
        {
            log::info("post process for yolov5 model (auto-detected)");
            int n = post_process_yolov5(outputs, img,
                                        inputs_info[0].shape[3],  // model_w
                                        inputs_info[0].shape[2]); // model_h
            // Build result path: strip directory, insert "_result" before extension
            std::string ip = std::string(img_path);
            std::string base = ip;
            {
                size_t sl = ip.rfind('/');
                if (sl != std::string::npos) base = ip.substr(sl + 1);
            }
            std::string stem = base, ext = ".jpg";
            {
                size_t dot = base.rfind('.');
                if (dot != std::string::npos) { stem = base.substr(0, dot); ext = base.substr(dot); }
            }
            std::string result_path = "/root/" + stem + "_result" + ext;
            err::Err serr = img->save(result_path.c_str(), 92);
            if(serr == err::ERR_NONE)
                log::info("annotated image saved -> %s  (%d detection(s))",
                          result_path.c_str(), n);
            else
                log::error("save image failed: err=%d  path=%s", (int)serr, result_path.c_str());
        }
        else
        {
            log::info("no post process for model type: %s, ignore", model_type.c_str());
        }
    }

end:
    if(outputs)
        delete outputs;
    if(img)
        delete img;

    log::info("Program exit");

    return ret;
}

int main(int argc, char* argv[])
{
    // Catch signal and process
    sys::register_default_signal_handle();

    // Use CATCH_EXCEPTION_RUN_RETURN to catch exception,
    // if we don't catch exception, when program throw exception, the objects will not be destructed.
    // So we catch exception here to let resources be released(call objects' destructor) before exit.
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}


