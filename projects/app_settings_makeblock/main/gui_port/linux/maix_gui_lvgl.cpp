
#include "maix_gui.hpp"
#include "maix_time.hpp"
#include "maix_basic.hpp"
#include "maix_lvgl.hpp"
// #include "./mousewheel.h"
// #include <SDL2/SDL.h>
#include <unistd.h>
#include "lvgl.h"
#include <assert.h>
#include <mutex>
#include <pthread.h>

// #define SDL_MAIN_HANDLED /*To fix SDL's "undefined reference to WinMain" issue*/
// #define _DEFAULT_SOURCE /* needed for usleep() */

template class Maix_GUI<MAIX_GUI_OBJ_T>;

template <typename T_OBJ>
Maix_GUI<T_OBJ>::Maix_GUI(bool ret_gesture)
    : default_font(NULL), ret_gesture(ret_gesture)
{
}

template <typename T_OBJ>
Maix_GUI<T_OBJ>::~Maix_GUI()
{
    if (default_font)
    {
        lv_binfont_destroy((lv_font_t *)default_font);
        default_font = NULL;
    }
    if(main_activity)
        delete main_activity;
}

// extern "C"
// {
//     static void root_event_cb(lv_event_t *e)
//     {
//         static lv_point_t x_start, x_end;
//         maix_gui_event_cb callback = NULL;
//         lv_event_code_t code = lv_event_get_code(e);
//         lv_obj_t *obj = lv_event_get_target(e);
//         lv_indev_t *indev = lv_indev_get_act();
//         callback = (maix_gui_event_cb)e->user_data;
//         if (code == LV_EVENT_PRESSED)
//         {
//             // get x_start where finger down
//             lv_indev_get_point(indev, &x_start);
//         }
//         else if (code == LV_EVENT_RELEASED || code == LV_EVENT_DEFOCUSED)
//         {
//             // get x_end where finger release
//             lv_indev_get_point(indev, &x_end);
//             if (x_start.x >= 0 && x_start.x <= 4 && x_end.x - x_start.x > 20)
//             {
//                 if (callback)
//                 {
//                     callback(obj, MAIX_GUI_EVENT_RETURN, NULL);
//                 }
//             }
//             x_start.x = -1;
//         }
//     }
// }

template <typename T_OBJ>
int Maix_GUI<T_OBJ>::init(display::Display *screen, touchscreen::TouchScreen *touchscreen)
{
    /*Initialize LVGL*/
    lvgl_init(screen, touchscreen);

    this->screen = screen;
    this->touchscreen = touchscreen;

    // create root activity
    main_activity = new Maix_GUI_Activity(NULL, false, NULL, NULL, NULL, NULL, NULL, NULL, "main", ret_gesture);
    return 0;
}

template <typename T_OBJ>
Maix_GUI_Activity* Maix_GUI<T_OBJ>::get_main_activity()
{
    return main_activity;
}

static pthread_mutex_t g_lv_ui_mutex = PTHREAD_MUTEX_INITIALIZER;
bool lv_ui_mutex_lock(int timeout)
{
    // lv_ui_mutex.lock();
    if(timeout < 0)
        pthread_mutex_lock(&g_lv_ui_mutex);
    else if(timeout == 0)
    {
        if(pthread_mutex_trylock(&g_lv_ui_mutex) != 0)
        {
            return false;
        }
    }
    else
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout / 1000;
        ts.tv_nsec += (timeout % 1000) * 1000000;
        if(ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        if(pthread_mutex_timedlock(&g_lv_ui_mutex, &ts) != 0)
        {
            return false;
        }
    }
    return true;
}

void lv_ui_mutex_unlock()
{
    // lv_ui_mutex.unlock();
    pthread_mutex_unlock(&g_lv_ui_mutex);
}


template <typename T_OBJ>
void Maix_GUI<T_OBJ>::run()
{
    // enable recursive mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_lv_ui_mutex, &attr);
    while (!app::need_exit())
    {
        /* Periodically call the lv_task handler.
         * It could be done in a timer interrupt or an OS task too.*/
        lv_ui_mutex_lock(-1);
        lv_timer_handler();
        lv_ui_mutex_unlock();
        time::sleep_us(1000);
        if(!this->screen->is_opened())
        {
            break;
        }
    }
    lvgl_destroy();
}

template <typename T_OBJ>
T_OBJ *Maix_GUI<T_OBJ>::get_root_panel(T_OBJ *obj)
{
    return lv_scr_act();
}

template <typename T_OBJ>
T_OBJ *Maix_GUI<T_OBJ>::get_default_font()
{
    if (!default_font)
    {
        default_font = (T_OBJ *)lv_binfont_create("A:/fonts/font_default.bin");
    }
    return default_font;
}


/*******************************************
 * Maix_GUI_Activity
 *******************************************/

class Activity_LVGL
{
public:
    Activity_LVGL(Maix_GUI_Activity* activity, lv_obj_t *parent, lv_obj_t *scr = NULL,
                maix_gui_init_ui_cb init_ui_cb = NULL, void *init_args_block = NULL,
                maix_gui_destroy_gui_cb destroy_callback = NULL, void *destroy_args = NULL,
                string id="-", bool ret_gesture_make=true)
    {
        this->activity = activity;
        this->parent_scr = parent;
        this->scr_arg = scr;
        this->activated = false;
        this->id = id;
        this->on_init_ui = init_ui_cb;
        this->on_destroy = destroy_callback;
        this->ret_gesture = ret_gesture_make;
        this->init_args = init_args_block;
        this->destroy_args = destroy_args;
        this->left_obj = nullptr;
    }
    ~Activity_LVGL()
    {
        printf("Activity_LVGL::~Activity_LVGL, id:%s\n", id.c_str());
        destroy();
    }
    void create_ui()
    {
        if (!scr_arg)
            this->scr = lv_obj_create(NULL);
        else
            this->scr = this->scr_arg;
        if(!ret_gesture)
        {
            this->left_obj = nullptr;
            return;
        }
        LV_IMG_DECLARE(img_return);
        left_obj = lv_btn_create(scr);
        lv_obj_align(left_obj, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(left_obj, lv_color_hex(0x3a3a3c), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(left_obj, LV_OPA_10, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(left_obj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(left_obj, 20, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(left_obj, 15, LV_PART_MAIN);
        lv_obj_add_event_cb(left_obj, this->return_event_cb3, LV_EVENT_CLICKED, activity);
        // add image icon
        lv_obj_t *img = lv_img_create(left_obj);
        lv_img_set_src(img, &img_return);

        // lv_obj_set_size(left_obj, 2, lv_pct(100));
        // lv_obj_clear_flag(left_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
        // lv_obj_add_event_cb(left_obj, this->return_event_cb, LV_EVENT_PRESSING, NULL);
        // lv_obj_add_event_cb(left_obj, this->return_event_cb2, LV_EVENT_RELEASED, activity);
        // lv_obj_set_style_bg_color(left_obj, lv_color_hex(0x9e9e9e), LV_PART_MAIN);
        // lv_obj_set_style_bg_opa(left_obj, LV_OPA_10, 0);
        // lv_obj_set_style_border_width(left_obj, 0, LV_PART_MAIN);
        // lv_obj_set_style_radius(left_obj, 0, LV_PART_MAIN);
        // lv_obj_clear_flag(left_obj, LV_OBJ_FLAG_SCROLLABLE);
    }
    void set_init_ui_cb(maix_gui_init_ui_cb cb, void *args)
    {
        this->on_init_ui = cb;
        this->init_args = args;
    }
    void set_destroy_cb(maix_gui_destroy_gui_cb cb, void *args)
    {
        this->on_destroy = cb;
        this->destroy_args = args;
    }

    void active(Maix_Activity_MSG *msg = nullptr)
    {
        if(!activated)
        {
            printf("active id: %s\n", id.c_str());
            printf("draw ui\n");
            lv_ui_mutex_lock(-1);
            create_ui();
            if(on_init_ui)
            {
                on_init_ui(activity, scr, msg, init_args);
                if(msg)
                    msg->destroy();
            }
            lv_ui_mutex_unlock();
            printf("draw user ui complete\n");
            lv_ui_mutex_lock(-1);
            if(left_obj)
                lv_obj_move_foreground(left_obj);
            if (lv_scr_act() != scr)
                lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
            lv_ui_mutex_unlock();
        }
        activated = true;
    }
    void destroy(Maix_Activity_MSG *msg = nullptr)
    {
        if(activated)
        {
            printf("destroy id: %s\n", id.c_str());
            if(on_destroy)
            {
                lv_ui_mutex_lock(-1);
                on_destroy(activity, scr, msg, destroy_args);
                lv_ui_mutex_unlock();
            }
            // lv_scr_load(parent_activity);
            if (parent_scr)
            {
                lv_ui_mutex_lock(-1);
                // lv_screen_load_anim(parent_scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true); // have bug, will always scroll flex to end
                lv_screen_load(parent_scr);
                lv_ui_mutex_unlock();
            }
            else
            {
                // root activity, do nothing
            }
        }
        activated = false;
    }
    static void return_event_cb(lv_event_t *e)
    {
        lv_obj_t *obj = (lv_obj_t*)lv_event_get_target(e);
        lv_indev_t *indev = lv_indev_get_act();
        if (indev == NULL)
            return;

        lv_point_t vect;
        lv_area_t coord;
        lv_indev_get_vect(indev, &vect);
        lv_obj_get_coords(obj, &coord);

        lv_coord_t x = coord.x1 + vect.x;
        lv_coord_t y = coord.y1 + vect.y;
        lv_obj_set_pos(obj, x, y);
        lv_obj_update_layout(obj); // force render to make lv_obj_get_x() work
    }
    static void return_event_cb2(lv_event_t *e)
    {
        lv_obj_t *obj = (lv_obj_t*)lv_event_get_target(e);
        lv_indev_t *indev = lv_indev_get_act();
        if (indev == NULL)
            return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);

        lv_coord_t x = lv_obj_get_x(obj) + vect.x;
        if (x >= 20)
        {
            Maix_GUI_Activity *activity = (Maix_GUI_Activity *)lv_event_get_user_data(e);
            activity->destroy();
        }
        lv_obj_set_pos(obj, 0, 0);
        lv_obj_update_layout(obj); // force render to make lv_obj_get_x() work
    }
    static void return_event_cb3(lv_event_t *e)
    {
        Maix_GUI_Activity *activity = (Maix_GUI_Activity *)lv_event_get_user_data(e);
        activity->destroy();
    }

    bool is_active()
    {
        return activated;
    }

public:
    lv_obj_t *scr;
    string id;
    bool ret_gesture;
private:
    Maix_GUI_Activity* activity;
    lv_obj_t *scr_arg;
    lv_obj_t *left_obj;
    lv_obj_t *parent_scr;
    bool activated;
    maix_gui_init_ui_cb on_init_ui;
    maix_gui_destroy_gui_cb on_destroy;
    void *init_args;
    void *destroy_args;
};

Maix_GUI_Activity::Maix_GUI_Activity(Maix_GUI_Activity *parent, bool auto_delete,
                                    maix_gui_init_ui_cb init_callback, void *init_args,
                                    maix_gui_before_destroy_gui_cb before_destroy_callback, void *before_destroy_args,
                                    maix_gui_destroy_gui_cb destroy_callback, void *destroy_args,
                                    string id, bool ret_gesture)
    : id(id), ret_gesture(ret_gesture), auto_delete(auto_delete), parent(parent), active_child(NULL),
    on_init_ui(init_callback), on_before_destroy(before_destroy_callback), on_destroy(destroy_callback), init_args(init_args), before_destroy_args(before_destroy_args), destroy_args(destroy_args)
{
    printf("==> Maix_GUI_Activity: %s\n", id.c_str());
    data = new Activity_LVGL(this, parent ? ((Activity_LVGL *)(parent->data))->scr : NULL,
                             parent ? NULL : lv_scr_act(),
                             init_callback, init_args,
                             destroy_callback, destroy_args,
                             id, ret_gesture);
}

Maix_GUI_Activity::~Maix_GUI_Activity()
{
    printf("<== ~Maix_GUI_Activity: %s\n", id.c_str());
    this->_destroy();
    delete (Activity_LVGL*)data;
}

void Maix_GUI_Activity::set_init_ui_cb(maix_gui_init_ui_cb callback, void *args)
{
    on_init_ui = callback;
    ((Activity_LVGL*)data)->set_init_ui_cb(callback, args);
}

void Maix_GUI_Activity::set_destroy_cb(maix_gui_destroy_gui_cb callback, void *args)
{
    on_destroy = callback;
    ((Activity_LVGL*)data)->set_destroy_cb(callback, args);
}

void Maix_GUI_Activity::active(Maix_Activity_MSG *msg)
{
    if(parent)
    {
        // parent must have only one active child
        assert(parent->active_child == NULL);
        parent->active_child = this;
    }
    ((Activity_LVGL *)data)->active(msg);
}

void Maix_GUI_Activity::_destroy(Maix_Activity_MSG *msg)
{
    // child activity must be destroyed first
    assert(active_child == NULL);
    if(((Activity_LVGL *)data)->is_active() && on_before_destroy)
    {
        on_before_destroy(this, nullptr, before_destroy_args);
    }
    ((Activity_LVGL *)data)->destroy(msg);
    if(parent)
    {
        parent->active_child = NULL;
    }
    else
    {
        app::set_exit_flag(true);
    }
}

void Maix_GUI_Activity::destroy(Maix_Activity_MSG *msg)
{
    printf("== Maix_GUI_Activity.destroy: %s, auto_delete: %d\n", id.c_str(), auto_delete);
    _destroy(msg);
    if(msg)
        msg->destroy();
    if(auto_delete)
    {
        delete this;
    }
}

void* Maix_GUI_Activity::get_raw()
{
    return ((Activity_LVGL*)data)->scr;
}

