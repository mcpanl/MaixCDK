#define _DEFAULT_SOURCE

#include "rk_lib.h"

#include <errno.h>
#include <pthread.h>
#include <sys/select.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#define RK_VO_OL_LINES 8
#define RK_TOUCH_Q_CAP 128

typedef struct rk_touch_event {
    int x;
    int y;
    int pressed;
} rk_touch_event_t;

struct rk_vo_ctx {
    Display *dpy;
    Window win;
    GC gc;
    int screen;
    unsigned int width;
    unsigned int height;
    XImage *ximage;
    char *img_data;
    Atom wm_protocols;
    Atom wm_delete;
    int should_close;
    uint8_t *last_rgb;
    unsigned int last_w;
    unsigned int last_h;
    unsigned int last_stride;
    int has_last;
    XFontStruct *font;
    char ol_text[RK_VO_OL_LINES][96];
    int ol_x[RK_VO_OL_LINES];
    int ol_y[RK_VO_OL_LINES];
    int ol_count;
    int ol_white;
    char title[128];
    unsigned int src_w;
    unsigned int src_h;
    int dst_x;
    int dst_y;
    unsigned int dst_w;
    unsigned int dst_h;
    int touch_x;
    int touch_y;
    int touch_pressed;
    rk_touch_event_t touch_q[RK_TOUCH_Q_CAP];
    unsigned int touch_q_head;
    unsigned int touch_q_tail;
};

static rk_vo_ctx_t *g_active_vo_ctx = NULL;
static pthread_mutex_t g_vo_api_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_x11_thread_once = PTHREAD_ONCE_INIT;

static void vo_init_x11_threads_once(void)
{
    XInitThreads();
}

static const char *vo_default_title(void)
{
    static char title[128];
    static int inited = 0;
    if (inited)
        return title;
    inited = 1;

    ssize_t n = readlink("/proc/self/exe", title, sizeof(title) - 1);
    if (n > 0) {
        title[n] = '\0';
        const char *base = strrchr(title, '/');
        if (base && base[1] != '\0')
            memmove(title, base + 1, strlen(base + 1) + 1);
        if (title[0] != '\0')
            return title;
    }
    strncpy(title, "MaixCDK", sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    return title;
}

static void vo_set_title(rk_vo_ctx_t *ctx, const char *title)
{
    const char *real_title = (title && title[0] != '\0') ? title : vo_default_title();
    strncpy(ctx->title, real_title, sizeof(ctx->title) - 1);
    ctx->title[sizeof(ctx->title) - 1] = '\0';
    XStoreName(ctx->dpy, ctx->win, ctx->title);
}

static void vo_compute_contain_rect(unsigned int win_w, unsigned int win_h, unsigned int src_w,
                                    unsigned int src_h, int *out_x, int *out_y, unsigned int *out_w,
                                    unsigned int *out_h)
{
    if (win_w == 0 || win_h == 0 || src_w == 0 || src_h == 0) {
        *out_x = 0;
        *out_y = 0;
        *out_w = win_w;
        *out_h = win_h;
        return;
    }

    unsigned long long lhs = (unsigned long long)win_w * (unsigned long long)src_h;
    unsigned long long rhs = (unsigned long long)win_h * (unsigned long long)src_w;

    if (lhs <= rhs) {
        *out_w = win_w;
        *out_h = (unsigned int)((unsigned long long)win_w * (unsigned long long)src_h /
                                (unsigned long long)src_w);
    } else {
        *out_h = win_h;
        *out_w = (unsigned int)((unsigned long long)win_h * (unsigned long long)src_w /
                                (unsigned long long)src_h);
    }
    if (*out_w == 0)
        *out_w = 1;
    if (*out_h == 0)
        *out_h = 1;

    *out_x = (int)(win_w - *out_w) / 2;
    *out_y = (int)(win_h - *out_h) / 2;
}

static int vo_touch_q_empty(const rk_vo_ctx_t *ctx)
{
    return ctx->touch_q_head == ctx->touch_q_tail;
}

static void vo_touch_q_push(rk_vo_ctx_t *ctx, int x, int y, int pressed)
{
    unsigned int next = (ctx->touch_q_tail + 1U) % RK_TOUCH_Q_CAP;
    if (next == ctx->touch_q_head) {
        ctx->touch_q_head = (ctx->touch_q_head + 1U) % RK_TOUCH_Q_CAP;
    }
    ctx->touch_q[ctx->touch_q_tail].x = x;
    ctx->touch_q[ctx->touch_q_tail].y = y;
    ctx->touch_q[ctx->touch_q_tail].pressed = pressed ? 1 : 0;
    ctx->touch_q_tail = next;
}

static int vo_touch_q_pop(rk_vo_ctx_t *ctx, rk_touch_event_t *out)
{
    if (vo_touch_q_empty(ctx))
        return -1;
    *out = ctx->touch_q[ctx->touch_q_head];
    ctx->touch_q_head = (ctx->touch_q_head + 1U) % RK_TOUCH_Q_CAP;
    return 0;
}

static void vo_free_last(rk_vo_ctx_t *ctx)
{
    free(ctx->last_rgb);
    ctx->last_rgb = NULL;
    ctx->has_last = 0;
    ctx->last_w = ctx->last_h = ctx->last_stride = 0;
}

static void put_rgb_to_ximage(rk_vo_ctx_t *ctx, const uint8_t *rgb, unsigned int sw, unsigned int sh,
                              unsigned int stride)
{
    unsigned int dw = ctx->width;
    unsigned int dh = ctx->height;
    int bpp = ctx->ximage->bits_per_pixel;
    int bytes_pp = bpp / 8;
    if (bytes_pp <= 0)
        return;

    unsigned char *data = (unsigned char *)ctx->ximage->data;
    memset(data, 0, (size_t)ctx->ximage->bytes_per_line * dh);

    vo_compute_contain_rect(dw, dh, sw, sh, &ctx->dst_x, &ctx->dst_y, &ctx->dst_w, &ctx->dst_h);
    ctx->src_w = sw;
    ctx->src_h = sh;

    unsigned int draw_w = ctx->dst_w;
    unsigned int draw_h = ctx->dst_h;
    int draw_x = ctx->dst_x;
    int draw_y = ctx->dst_y;

    for (unsigned int y = 0; y < dh; y++) {
        if ((int)y < draw_y || (int)y >= draw_y + (int)draw_h)
            continue;
        unsigned int ly = y - (unsigned int)draw_y;
        unsigned int sy = ly * sh / draw_h;
        const uint8_t *src_row = rgb + sy * stride;
        unsigned char *dst_row = data + y * ctx->ximage->bytes_per_line;
        for (unsigned int x = 0; x < dw; x++) {
            if ((int)x < draw_x || (int)x >= draw_x + (int)draw_w)
                continue;
            unsigned int lx = x - (unsigned int)draw_x;
            unsigned int sx = lx * sw / draw_w;
            const uint8_t *s = src_row + sx * 3;
            unsigned char *d = dst_row + x * bytes_pp;
            if (bpp >= 32) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 0;
            } else if (bpp >= 24) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
            } else if (bpp >= 16) {
                /* RGB888 -> RGB565 for 16bpp visuals */
                uint16_t r = (uint16_t)(s[0] >> 3);
                uint16_t g = (uint16_t)(s[1] >> 2);
                uint16_t b = (uint16_t)(s[2] >> 3);
                uint16_t pix = (uint16_t)((r << 11) | (g << 5) | b);
                memcpy(d, &pix, sizeof(pix));
            } else {
                /* fallback: luminance for very low bpp visuals */
                d[0] = (unsigned char)(((int)s[0] * 30 + (int)s[1] * 59 + (int)s[2] * 11) / 100);
            }
        }
    }
}

static int vo_copy_last(rk_vo_ctx_t *ctx, const uint8_t *rgb, unsigned int w, unsigned int h,
                        unsigned int stride)
{
    size_t need = (size_t)w * h * 3;
    if (!ctx->last_rgb || ctx->last_w != w || ctx->last_h != h) {
        vo_free_last(ctx);
        ctx->last_rgb = (uint8_t *)malloc(need);
        if (!ctx->last_rgb)
            return -ENOMEM;
        ctx->last_w = w;
        ctx->last_h = h;
    }
    ctx->last_stride = stride;
    for (unsigned int y = 0; y < h; y++)
        memcpy(ctx->last_rgb + y * w * 3, rgb + y * stride, w * 3);
    ctx->has_last = 1;
    return 0;
}

static int vo_font_line_height(const XFontStruct *f)
{
    return (int)f->ascent + (int)f->descent + 2;
}

static void vo_redraw_overlay(rk_vo_ctx_t *ctx)
{
    if (!ctx->ol_count || !ctx->font || !ctx->dpy || !ctx->win || !ctx->gc)
        return;
    unsigned long fg =
        ctx->ol_white ? WhitePixel(ctx->dpy, ctx->screen) : BlackPixel(ctx->dpy, ctx->screen);
    XSetForeground(ctx->dpy, ctx->gc, fg);
    for (int i = 0; i < ctx->ol_count; i++) {
        int baseline = ctx->ol_y[i] + (int)ctx->font->ascent;
        XDrawString(ctx->dpy, ctx->win, ctx->gc, ctx->ol_x[i], baseline, ctx->ol_text[i],
                    (int)strlen(ctx->ol_text[i]));
    }
}

static void vo_dispatch_event(rk_vo_ctx_t *ctx, XEvent *ev)
{
    Display *dpy = ctx->dpy;
    switch (ev->type) {
    case Expose:
        if (ev->xexpose.count == 0 && ctx->ximage && ctx->gc) {
            XPutImage(dpy, ctx->win, ctx->gc, ctx->ximage, 0, 0, 0, 0, ctx->width, ctx->height);
            vo_redraw_overlay(ctx);
        }
        break;
    case ClientMessage:
        if (ev->xclient.message_type == ctx->wm_protocols &&
            (Atom)ev->xclient.data.l[0] == ctx->wm_delete)
            ctx->should_close = 1;
        break;
    case ConfigureNotify: {
        unsigned int nw = (unsigned int)ev->xconfigure.width;
        unsigned int nh = (unsigned int)ev->xconfigure.height;
        if (nw > 0 && nh > 0 && (nw != ctx->width || nh != ctx->height)) {
            ctx->width = nw;
            ctx->height = nh;
            if (ctx->ximage) {
                ctx->ximage->data = NULL;
                XDestroyImage(ctx->ximage);
                ctx->ximage = NULL;
            }
            free(ctx->img_data);
            ctx->img_data = NULL;

            size_t img_bytes = (size_t)ctx->width * ctx->height * 4;
            ctx->img_data = (char *)calloc(1, img_bytes);
            if (ctx->img_data) {
                Visual *visual = DefaultVisual(ctx->dpy, ctx->screen);
                int depth = DefaultDepth(ctx->dpy, ctx->screen);
                ctx->ximage = XCreateImage(ctx->dpy, visual, depth, ZPixmap, 0, ctx->img_data,
                                           ctx->width, ctx->height, 32, 0);
                if (!ctx->ximage) {
                    free(ctx->img_data);
                    ctx->img_data = NULL;
                } else if (ctx->has_last) {
                    put_rgb_to_ximage(ctx, ctx->last_rgb, ctx->last_w, ctx->last_h, ctx->last_w * 3);
                }
            }
        }
        break;
    }
    case MotionNotify:
        if (ctx->dst_w == 0 || ctx->dst_h == 0)
            break;
        if (ev->xmotion.x < ctx->dst_x || ev->xmotion.y < ctx->dst_y ||
            ev->xmotion.x >= ctx->dst_x + (int)ctx->dst_w ||
            ev->xmotion.y >= ctx->dst_y + (int)ctx->dst_h) {
            break;
        }
        ctx->touch_x = (ev->xmotion.x - ctx->dst_x) * (int)ctx->src_w / (int)ctx->dst_w;
        ctx->touch_y = (ev->xmotion.y - ctx->dst_y) * (int)ctx->src_h / (int)ctx->dst_h;
        vo_touch_q_push(ctx, ctx->touch_x, ctx->touch_y, ctx->touch_pressed);
        break;
    case ButtonPress:
        if (ev->xbutton.button == Button1) {
            if (ctx->dst_w == 0 || ctx->dst_h == 0)
                break;
            if (ev->xbutton.x < ctx->dst_x || ev->xbutton.y < ctx->dst_y ||
                ev->xbutton.x >= ctx->dst_x + (int)ctx->dst_w ||
                ev->xbutton.y >= ctx->dst_y + (int)ctx->dst_h) {
                break;
            }
            ctx->touch_pressed = 1;
            ctx->touch_x = (ev->xbutton.x - ctx->dst_x) * (int)ctx->src_w / (int)ctx->dst_w;
            ctx->touch_y = (ev->xbutton.y - ctx->dst_y) * (int)ctx->src_h / (int)ctx->dst_h;
            vo_touch_q_push(ctx, ctx->touch_x, ctx->touch_y, ctx->touch_pressed);
        }
        break;
    case ButtonRelease:
        if (ev->xbutton.button == Button1) {
            ctx->touch_pressed = 0;
            if (ctx->dst_w == 0 || ctx->dst_h == 0 || ctx->src_w == 0 || ctx->src_h == 0)
                break;
            int px = ev->xbutton.x;
            int py = ev->xbutton.y;
            if (px < ctx->dst_x)
                px = ctx->dst_x;
            if (py < ctx->dst_y)
                py = ctx->dst_y;
            if (px >= ctx->dst_x + (int)ctx->dst_w)
                px = ctx->dst_x + (int)ctx->dst_w - 1;
            if (py >= ctx->dst_y + (int)ctx->dst_h)
                py = ctx->dst_y + (int)ctx->dst_h - 1;
            ctx->touch_x = (px - ctx->dst_x) * (int)ctx->src_w / (int)ctx->dst_w;
            ctx->touch_y = (py - ctx->dst_y) * (int)ctx->src_h / (int)ctx->dst_h;
            vo_touch_q_push(ctx, ctx->touch_x, ctx->touch_y, ctx->touch_pressed);
        }
        break;
    default:
        break;
    }
}

static int vo_touch_wait_fd_ready(rk_vo_ctx_t *ctx, int timeout_ms)
{
    int fd = ConnectionNumber(ctx->dpy);
    if (fd < 0)
        return -EINVAL;

    if (XPending(ctx->dpy) > 0)
        return 1;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    if (timeout_ms < 0) {
        int ret = select(fd + 1, &rfds, NULL, NULL, NULL);
        return (ret > 0) ? 1 : (ret == 0 ? 0 : -errno);
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
    return (ret > 0) ? 1 : (ret == 0 ? 0 : -errno);
}

static int vo_touch_pump_events(rk_vo_ctx_t *ctx, int timeout_ms)
{
    int wait_ret = vo_touch_wait_fd_ready(ctx, timeout_ms);
    if (wait_ret <= 0)
        return wait_ret;

    while (XPending(ctx->dpy) > 0) {
        XEvent ev;
        XNextEvent(ctx->dpy, &ev);
        vo_dispatch_event(ctx, &ev);
    }
    return 1;
}

static rk_vo_ctx_t *vo_touch_resolve_ctx(rk_vo_ctx_t *ctx)
{
    if (ctx)
        return ctx;
    return g_active_vo_ctx;
}

static long long vo_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
}

static XFontStruct *vo_load_font(Display *dpy)
{
    static const char *const names[] = {"fixed", "6x13", "9x15", "8x13"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        XFontStruct *f = XLoadQueryFont(dpy, names[i]);
        if (f)
            return f;
    }
    return NULL;
}

int rk_vo_open2(rk_vo_ctx_t **out_ctx, const char *display_name, unsigned int w, unsigned int h,
                const char *title)
{
    pthread_mutex_lock(&g_vo_api_lock);
    pthread_once(&g_x11_thread_once, vo_init_x11_threads_once);

    rk_vo_ctx_t *ctx = (rk_vo_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -ENOMEM;
    }

    ctx->dpy = XOpenDisplay(display_name);
    if (!ctx->dpy) {
        free(ctx);
        pthread_mutex_unlock(&g_vo_api_lock);
        return -EIO;
    }
    ctx->screen = DefaultScreen(ctx->dpy);
    ctx->width = w;
    ctx->height = h;

    Window root = RootWindow(ctx->dpy, ctx->screen);
    Visual *visual = DefaultVisual(ctx->dpy, ctx->screen);
    int depth = DefaultDepth(ctx->dpy, ctx->screen);

    ctx->win = XCreateSimpleWindow(ctx->dpy, root, 0, 0, w, h, 0, BlackPixel(ctx->dpy, ctx->screen),
                                   WhitePixel(ctx->dpy, ctx->screen));
    XSelectInput(ctx->dpy, ctx->win,
                 ExposureMask | StructureNotifyMask | KeyPressMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

    ctx->wm_protocols = XInternAtom(ctx->dpy, "WM_PROTOCOLS", False);
    ctx->wm_delete = XInternAtom(ctx->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ctx->dpy, ctx->win, &ctx->wm_delete, 1);

    ctx->gc = XCreateGC(ctx->dpy, ctx->win, 0, NULL);
    vo_set_title(ctx, title);
    XMapWindow(ctx->dpy, ctx->win);
    XFlush(ctx->dpy);

    size_t img_bytes = (size_t)w * h * 4;
    ctx->img_data = (char *)calloc(1, img_bytes);
    if (!ctx->img_data) {
        XFreeGC(ctx->dpy, ctx->gc);
        XDestroyWindow(ctx->dpy, ctx->win);
        XCloseDisplay(ctx->dpy);
        free(ctx);
        return -ENOMEM;
    }

    ctx->ximage =
        XCreateImage(ctx->dpy, visual, depth, ZPixmap, 0, ctx->img_data, w, h, 32, 0);
    if (!ctx->ximage) {
        free(ctx->img_data);
        XFreeGC(ctx->dpy, ctx->gc);
        XDestroyWindow(ctx->dpy, ctx->win);
        XCloseDisplay(ctx->dpy);
        free(ctx);
        return -ENOMEM;
    }

    ctx->font = vo_load_font(ctx->dpy);
    ctx->ol_count = 0;
    ctx->src_w = w;
    ctx->src_h = h;
    ctx->dst_x = 0;
    ctx->dst_y = 0;
    ctx->dst_w = w;
    ctx->dst_h = h;
    ctx->touch_x = 0;
    ctx->touch_y = 0;
    ctx->touch_pressed = 0;
    ctx->touch_q_head = 0;
    ctx->touch_q_tail = 0;

    *out_ctx = ctx;
    g_active_vo_ctx = ctx;
    pthread_mutex_unlock(&g_vo_api_lock);
    return 0;
}

int rk_vo_open(rk_vo_ctx_t **out_ctx, const char *display_name, unsigned int w, unsigned int h)
{
    return rk_vo_open2(out_ctx, display_name, w, h, NULL);
}

void rk_vo_close(rk_vo_ctx_t *ctx)
{
    pthread_mutex_lock(&g_vo_api_lock);
    if (!ctx)
    {
        pthread_mutex_unlock(&g_vo_api_lock);
        return;
    }
    vo_free_last(ctx);
    if (ctx->font) {
        XFreeFont(ctx->dpy, ctx->font);
        ctx->font = NULL;
    }
    if (ctx->ximage) {
        ctx->ximage->data = NULL;
        XDestroyImage(ctx->ximage);
        ctx->ximage = NULL;
    }
    free(ctx->img_data);
    ctx->img_data = NULL;
    if (ctx->gc) {
        XFreeGC(ctx->dpy, ctx->gc);
        ctx->gc = NULL;
    }
    if (ctx->win) {
        XDestroyWindow(ctx->dpy, ctx->win);
        ctx->win = 0;
    }
    if (ctx->dpy) {
        XCloseDisplay(ctx->dpy);
        ctx->dpy = NULL;
    }
    if (g_active_vo_ctx == ctx)
        g_active_vo_ctx = NULL;
    free(ctx);
    pthread_mutex_unlock(&g_vo_api_lock);
}

int rk_vo_put_rgb888(rk_vo_ctx_t *ctx, const uint8_t *rgb, unsigned int w, unsigned int h,
                     unsigned int stride)
{
    pthread_mutex_lock(&g_vo_api_lock);
    if (!ctx || !ctx->dpy || !ctx->ximage || !rgb)
    {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -EINVAL;
    }
    int r = vo_copy_last(ctx, rgb, w, h, stride);
    if (r < 0) {
        pthread_mutex_unlock(&g_vo_api_lock);
        return r;
    }
    put_rgb_to_ximage(ctx, rgb, w, h, stride);
    XPutImage(ctx->dpy, ctx->win, ctx->gc, ctx->ximage, 0, 0, 0, 0, ctx->width, ctx->height);
    vo_redraw_overlay(ctx);
    XFlush(ctx->dpy);
    pthread_mutex_unlock(&g_vo_api_lock);
    return 0;
}

int rk_vo_draw_string(rk_vo_ctx_t *ctx, int x, int y, const char *s, int white_fg)
{
    pthread_mutex_lock(&g_vo_api_lock);
    if (!ctx || !ctx->dpy || !s)
    {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -EINVAL;
    }
    if (!ctx->font)
    {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -ENOENT;
    }

    ctx->ol_white = white_fg ? 1 : 0;
    ctx->ol_count = 0;
    int line_h = vo_font_line_height(ctx->font);
    const char *p = s;

    while (*p && ctx->ol_count < RK_VO_OL_LINES) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (len >= sizeof(ctx->ol_text[0]))
            len = sizeof(ctx->ol_text[0]) - 1;
        memcpy(ctx->ol_text[ctx->ol_count], p, len);
        ctx->ol_text[ctx->ol_count][len] = '\0';
        ctx->ol_x[ctx->ol_count] = x;
        ctx->ol_y[ctx->ol_count] = y + ctx->ol_count * line_h;
        ctx->ol_count++;
        p = eol ? eol + 1 : "";
    }

    vo_redraw_overlay(ctx);
    XFlush(ctx->dpy);
    pthread_mutex_unlock(&g_vo_api_lock);
    return 0;
}

int rk_vo_poll_events(rk_vo_ctx_t *ctx, int block)
{
    pthread_mutex_lock(&g_vo_api_lock);
    if (!ctx || !ctx->dpy)
    {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -EINVAL;
    }

    if (!block) {
        while (XPending(ctx->dpy) > 0) {
            XEvent ev;
            XNextEvent(ctx->dpy, &ev);
            vo_dispatch_event(ctx, &ev);
        }
        int ret = ctx->should_close ? 0 : 1;
        pthread_mutex_unlock(&g_vo_api_lock);
        return ret;
    }

    XEvent ev;
    XNextEvent(ctx->dpy, &ev);
    vo_dispatch_event(ctx, &ev);
    int ret = ctx->should_close ? 0 : 1;
    pthread_mutex_unlock(&g_vo_api_lock);
    return ret;
}

int rk_vo_touch_available(rk_vo_ctx_t *ctx, int timeout_ms)
{
    long long deadline = -1;
    if (timeout_ms > 0)
        deadline = vo_now_ms() + (long long)timeout_ms;

    while (1) {
        pthread_mutex_lock(&g_vo_api_lock);
        rk_vo_ctx_t *real_ctx = vo_touch_resolve_ctx(ctx);
        if (!real_ctx || !real_ctx->dpy) {
            pthread_mutex_unlock(&g_vo_api_lock);
            return -ENODEV;
        }

        if (!vo_touch_q_empty(real_ctx)) {
            pthread_mutex_unlock(&g_vo_api_lock);
            return 1;
        }

        int ret = vo_touch_pump_events(real_ctx, 0);
        if (ret < 0) {
            pthread_mutex_unlock(&g_vo_api_lock);
            return ret;
        }
        if (!vo_touch_q_empty(real_ctx)) {
            pthread_mutex_unlock(&g_vo_api_lock);
            return 1;
        }
        pthread_mutex_unlock(&g_vo_api_lock);

        if (timeout_ms == 0)
            return 0;
        if (timeout_ms > 0 && vo_now_ms() >= deadline)
            return 0;

        usleep(2000);
    }
}

int rk_vo_touch_read(rk_vo_ctx_t *ctx, int *x, int *y, int *pressed, int drain_nonkey)
{
    pthread_mutex_lock(&g_vo_api_lock);
    rk_vo_ctx_t *real_ctx = vo_touch_resolve_ctx(ctx);
    rk_touch_event_t ev;

    if (!x || !y || !pressed) {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -EINVAL;
    }
    if (!real_ctx || !real_ctx->dpy) {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -ENODEV;
    }

    if (vo_touch_q_empty(real_ctx))
        vo_touch_pump_events(real_ctx, 0);
    if (vo_touch_q_empty(real_ctx)) {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -EAGAIN;
    }

    if (drain_nonkey) {
        while (vo_touch_q_pop(real_ctx, &ev) == 0) {
            *x = ev.x;
            *y = ev.y;
            *pressed = ev.pressed;
        }
        pthread_mutex_unlock(&g_vo_api_lock);
        return 0;
    }

    if (vo_touch_q_pop(real_ctx, &ev) != 0) {
        pthread_mutex_unlock(&g_vo_api_lock);
        return -EAGAIN;
    }

    *x = ev.x;
    *y = ev.y;
    *pressed = ev.pressed;
    pthread_mutex_unlock(&g_vo_api_lock);
    return 0;
}
