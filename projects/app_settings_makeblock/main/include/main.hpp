#pragma once

#include <string>
#include "HTTPRequest.hpp"
#include "maix_gui.hpp"
#include "maix_app.hpp"
#include <string>
#include <vector>
#include <stdio.h>
#include "i18n.hpp"


using namespace std;

#define APP_ID "settings"
#define DATA_INPUT_FILENAME "input.txt"
#define TEMP_APP_ZIP_FILENAME "app.zip"

#define theme_bg_color lv_color_hex(0x00000000)
#define theme_btn_color lv_color_hex(0x3a3a3c)
#define theme_text_color lv_color_hex(0xffffff)

extern int install_runtime();

extern display::Display *p_display;

typedef enum
{
    MSG_TYPE_RESULT = 1, // data: result_t*
    MSG_TYPE_URL         // data: string*
} msg_type_t;

typedef struct
{
    int code;
    string msg;
} result_t;


class Activity_MSG : public Maix_Activity_MSG
{
public:
    /**
     * @param auto_delete auto delete this object when call destroy method
    */
    Activity_MSG(msg_type_t type, bool auto_delete)
    : Maix_Activity_MSG(type, auto_delete)
    {
        if(type == MSG_TYPE_RESULT)
        {
            data = new result_t;
        }
        else if(type == MSG_TYPE_URL)
        {
            data = new string;
        }
    }
    ~Activity_MSG()
    {
        if(type == MSG_TYPE_RESULT)
        {
            result_t *result = (result_t*)data;
            delete result;
        }
        else if(type == MSG_TYPE_URL)
        {
            string *url = (string*)data;
            delete url;
        }
    }

public:
    void *data;
};

extern void on_language_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_language_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_wifi_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_wifi_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_iofo_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_iofo_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_comm_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_comm_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_about_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_about_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_upgrade_maixpy_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_upgrade_maixpy_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_usb_settings_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_usb_settings_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_poweroff_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_poweroff_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_upgrade_lib_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_upgrade_lib_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_auto_start_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_auto_start_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_backlight_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_backlight_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_volume_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_volume_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern void on_datetime_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);
extern void on_datetime_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args);

extern bool showing_msg;



