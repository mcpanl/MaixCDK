

#pragma once

#include "stdint.h"
#include "maix_gui_hal.hpp"
#include "maix_display.hpp"
#include "maix_touchscreen.hpp"
#include <string>
#include <assert.h>
using namespace std;
using namespace maix;

// enumerate event, MAIX_GUI_EVENT_RETURN
typedef enum {
} maix_gui_event_t;

class Maix_GUI_Activity;

class Maix_Activity_MSG {
public:
    Maix_Activity_MSG(int type, bool auto_delete)
    : type(type), auto_delete(auto_delete)
    {
    }
    void destroy()
    {
        if(auto_delete)
        {
            delete this;
        }
    }

public:
    int type;
    bool auto_delete;
};

typedef void (*maix_gui_init_ui_cb)(Maix_GUI_Activity *activity, void* obj, Maix_Activity_MSG* msg, void *args);
typedef void (*maix_gui_before_destroy_gui_cb)(Maix_GUI_Activity *activity, Maix_Activity_MSG* msg, void *args);
typedef void (*maix_gui_destroy_gui_cb)(Maix_GUI_Activity *activity, void* obj, Maix_Activity_MSG* msg, void *args);

template<typename T_OBJ>
class Maix_GUI {
public:
    Maix_GUI(bool ret_gesture=true);
    int init(display::Display *screen, touchscreen::TouchScreen *touchscreen);
    Maix_GUI_Activity* get_main_activity();
    void run();
    ~Maix_GUI();

    // UI related
    T_OBJ* get_root_panel(T_OBJ* obj);
    T_OBJ* get_default_font();

    display::Display *screen;
    touchscreen::TouchScreen *touchscreen;

private:
    T_OBJ* default_font;
    Maix_GUI_Activity* main_activity;
    bool ret_gesture;
};


class Maix_GUI_Activity {
public:
    /**
     * @param auto_delete auto delete this object when call destroy method
     * @param destroy_callback call in lv_ui_mutex_lock, so use it carefully!!!
    */
    Maix_GUI_Activity(Maix_GUI_Activity *parent, bool auto_delete,
                        maix_gui_init_ui_cb init_callback=NULL, void *init_args = NULL,
                        maix_gui_before_destroy_gui_cb before_destroy_callback=NULL, void *before_destroy_args = NULL,
                        maix_gui_destroy_gui_cb destroy_callback=NULL, void *destroy_args = NULL,
                        string id="-", bool ret_gesture=true);
    ~Maix_GUI_Activity();
    void set_init_ui_cb(maix_gui_init_ui_cb callback, void *args = NULL);
    void set_destroy_cb(maix_gui_destroy_gui_cb callback, void *args = NULL);
    void active(Maix_Activity_MSG *msg = nullptr);
    void destroy(Maix_Activity_MSG *msg = nullptr);
    void* get_raw();

public:
    string id;
    bool ret_gesture;
    bool auto_delete;
    void *user_data1 = NULL;
    void *user_data2 = NULL;

private:
    Maix_GUI_Activity* parent;
    Maix_GUI_Activity* active_child;
    void* data; // store implements' data
    maix_gui_init_ui_cb on_init_ui;
    maix_gui_before_destroy_gui_cb on_before_destroy;
    maix_gui_destroy_gui_cb on_destroy;
    void *init_args;
    void *before_destroy_args;
    void *destroy_args;
    void _destroy(Maix_Activity_MSG *msg = nullptr);
};


