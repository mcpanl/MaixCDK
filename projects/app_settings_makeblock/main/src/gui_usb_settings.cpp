#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_thread.hpp"
#include "maix_basic.hpp"
#include "maix_log.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>

static lv_obj_t *ddlist = NULL;
static lv_obj_t * sw_ncm = NULL;
static lv_obj_t * sw_rndis = NULL;
static lv_obj_t * sw_mouse = NULL;
static lv_obj_t * sw_keyboard = NULL;
static lv_obj_t * sw_touchpad = NULL;
static lv_obj_t * sw_uvc = NULL;

static bool is_usb_host_mode()
{
    return fs::exists("/boot/usb.host");
}

static void set_usb_mode_host()
{
    if(fs::exists("/boot/usb.dev"))
        fs::remove("/boot/usb.dev");
    fs::File *f = fs::open("/boot/usb.host", "w");
    if(!f)
    {
        log::error("open /boot/usb.host failed");
        return;
    }
    f->close();
    delete f;
    sync();
}

static void set_usb_mode_dev()
{
    if(fs::exists("/boot/usb.host"))
        fs::remove("/boot/usb.host");
    fs::File *f = fs::open("/boot/usb.dev", "w");
    if(!f)
    {
        log::error("open /boot/usb.dev failed");
        return;
    }
    f->close();
    delete f;
    sync();
}

static bool is_usb_rndis_on()
{
    return fs::exists("/boot/usb.rndis");
}

static bool is_usb_ncm_on()
{
    return fs::exists("/boot/usb.ncm");
}

static bool is_usb_mouse_on()
{
    return fs::exists("/boot/usb.mouse");
}


static bool is_usb_keyboard_on()
{
    return fs::exists("/boot/usb.keyboard");
}


static bool is_usb_touchpad_on()
{
    return fs::exists("/boot/usb.touchpad");
}


static void restart_usb()
{
    int result = system("/etc/init.d/S30gadget_nic stop");
    if(result != 0)
    {
        log::error("stop gadget NIC failed: %d", result);
    }
    result = system("/etc/init.d/S03usbdev stop && /etc/init.d/S03usbdev start");
    if(result != 0)
    {
        log::error("restart usb failed: %d", result);
    }
    result = system("/etc/init.d/S30gadget_nic start");
    if(result != 0)
    {
        log::error("start gadget NIC failed: %d", result);
    }
}

static void set_ncm(bool on)
{
    if(fs::exists("/boot/usb.ncm"))
        fs::remove("/boot/usb.ncm");
    if(on)
    {
        fs::File *f = fs::open("/boot/usb.ncm", "w");
        if(!f)
        {
            log::error("open /boot/usb.ncm failed");
            return;
        }
        f->close();
        delete f;
    }
    sync();
}

static void set_rndis(bool on)
{
    if(fs::exists("/boot/usb.rndis"))
        fs::remove("/boot/usb.rndis");
    if(on)
    {
        fs::File *f = fs::open("/boot/usb.rndis", "w");
        if(!f)
        {
            log::error("open /boot/usb.rndis failed");
            return;
        }
        f->close();
        delete f;
    }
    sync();
}

static void set_mouse(bool on)
{
    if(fs::exists("/boot/usb.mouse"))
        fs::remove("/boot/usb.mouse");
    if(on)
    {
        fs::File *f = fs::open("/boot/usb.mouse", "w");
        if(!f)
        {
            log::error("open /boot/usb.mouse failed");
            return;
        }
        f->close();
        delete f;
    }
    sync();
}

static void set_keyboard(bool on)
{
    if(fs::exists("/boot/usb.keyboard"))
        fs::remove("/boot/usb.keyboard");
    if(on)
    {
        fs::File *f = fs::open("/boot/usb.keyboard", "w");
        if(!f)
        {
            log::error("open /boot/usb.keyboard failed");
            return;
        }
        f->close();
        delete f;
    }
    sync();
}

static void set_touchpad(bool on)
{
    if(fs::exists("/boot/usb.touchpad"))
        fs::remove("/boot/usb.touchpad");
    if(on)
    {
        fs::File *f = fs::open("/boot/usb.touchpad", "w");
        if(!f)
        {
            log::error("open /boot/usb.touchpad failed");
            return;
        }
        f->close();
        delete f;
    }
    sync();
}

#define DEF_GADGET_FUNCTION(func, file) \
static bool is_usb_##func##_on() \
{ \
    const char path[] = "/boot/" #file; \
    return fs::exists(path); \
} \
static void set_##func(bool on) \
{ \
    const char path[] = "/boot/" #file; \
    const char path_bak[] = "/boot/" #file "~"; \
    if(fs::exists(path)) \
        fs::rename(path, path_bak); \
    if(on) \
    { \
        if(fs::exists(path_bak)) \
            fs::rename(path_bak, path); \
        else { \
            fs::File *f = fs::open(path, "w"); \
            if(!f) \
            { \
                log::error("open %s failed", path); \
                return; \
            } \
            f->close(); \
            delete f; \
        } \
    } \
    sync(); \
}

DEF_GADGET_FUNCTION(uvc, usb.uvc)



void on_usb_settings_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
    std::string locale = i18n::get_locale();

    lv_obj_t *root = (lv_obj_t *)obj;
    lv_obj_set_style_text_font(root, get_font_by_locale(locale), LV_PART_MAIN);

    lv_obj_t *container = lv_obj_create(root);
    lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(container, theme_bg_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(container, theme_text_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label_mode = lv_label_create(container);
    lv_label_set_text(label_mode, _("USB mode"));


    // add a drop down list
    ddlist = lv_dropdown_create(container);
    lv_obj_set_size(ddlist, 200, 50);
    lv_obj_align(ddlist, LV_ALIGN_CENTER, 0, 0);
    // set font
    // lv_obj_set_style_text_font(ddlist, &lv_font_montserrat_16, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ddlist, theme_btn_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(ddlist, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_color(ddlist, theme_btn_color, LV_PART_MAIN);

    lv_dropdown_set_options(ddlist, "device\nhost");
    // lv_obj_add_event_cb(ddlist, [](lv_event_t *e) {
    //     uint32_t code = lv_event_get_code(e);
    //     /*if (code == LV_EVENT_VALUE_CHANGED)
    //     {
    //         lv_obj_t *ddlist = lv_event_get_current_target(e);
    //         uint16_t idx = lv_dropdown_get_selected(ddlist);
    //         printf("select language: %s, %s\n", locales[idx].c_str(), langs_name[idx].c_str());
    //     }
    //     else */if (code == LV_EVENT_READY)
    //     {
    //         lv_obj_t *ddlist = (lv_obj_t *)lv_event_get_current_target(e);
    //         lv_obj_t *list = lv_dropdown_get_list(ddlist);
    //         lv_obj_set_style_bg_color(list, theme_btn_color, LV_PART_MAIN);
    //         lv_obj_set_style_text_color(list, lv_color_hex(0xffffff), LV_PART_MAIN);
    //         lv_obj_set_style_border_color(list, theme_btn_color, LV_PART_MAIN);
    //     }
    // }, LV_EVENT_READY, NULL);
    lv_dropdown_set_selected(ddlist, is_usb_host_mode() ? 1 : 0);

    // NIC mode, RNDIS or NCM
    lv_obj_t *label_nic_mode = lv_label_create(container);
    lv_label_set_text(label_nic_mode, _("NIC mode"));
    lv_obj_t *label_nic_mode_hint = lv_label_create(container);
    lv_label_set_text(label_nic_mode_hint, _("rndis for Windows, ncm for others"));

    // add switches
    lv_obj_t *row1 = lv_obj_create(container);
    lv_obj_set_size(row1, lv_pct(100), LV_SIZE_CONTENT);
    // lv_obj_set_style_bg_color(row1, theme_bg_color, LV_PART_MAIN);
    // lv_obj_set_style_text_color(row1, theme_text_color, LV_PART_MAIN);
    // lv_obj_set_style_border_width(row1, 0, LV_PART_MAIN);
    lv_obj_set_layout(row1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label_ncm = lv_label_create(row1);
    lv_label_set_text(label_ncm, trans.tr("CDC NCM").c_str());
    sw_ncm = lv_switch_create(row1);
    lv_obj_add_state(sw_ncm, is_usb_ncm_on() ? LV_STATE_CHECKED : LV_STATE_DEFAULT);

    lv_obj_t *label_rndis = lv_label_create(row1);
    lv_label_set_text(label_rndis, trans.tr("RNDIS").c_str());
    sw_rndis = lv_switch_create(row1);
    lv_obj_add_state(sw_rndis, is_usb_rndis_on() ? LV_STATE_CHECKED : LV_STATE_DEFAULT);


    // add switches
    lv_obj_t *row2 = lv_obj_create(container);
    lv_obj_set_size(row2, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row2, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label_mouse = lv_label_create(row2);
    lv_label_set_text(label_mouse, trans.tr("HID Mouse").c_str());
    sw_mouse = lv_switch_create(row2);
    lv_obj_add_state(sw_mouse, is_usb_mouse_on() ? LV_STATE_CHECKED : LV_STATE_DEFAULT);

    lv_obj_t *label_keyboard = lv_label_create(row2);
    lv_label_set_text(label_keyboard, trans.tr("HID Keyboard").c_str());
    sw_keyboard = lv_switch_create(row2);
    lv_obj_add_state(sw_keyboard, is_usb_keyboard_on() ? LV_STATE_CHECKED : LV_STATE_DEFAULT);

    // add switches
    lv_obj_t *row3 = lv_obj_create(container);
    lv_obj_set_size(row3, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row3, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label_touchpad = lv_label_create(row3);
    lv_label_set_text(label_touchpad, trans.tr("HID Touchpad").c_str());
    sw_touchpad = lv_switch_create(row3);
    lv_obj_add_state(sw_touchpad, is_usb_touchpad_on() ? LV_STATE_CHECKED : LV_STATE_DEFAULT);


    // add switches
    // UVC configuration
    lv_obj_t *row4 = lv_obj_create(container);
    lv_obj_set_size(row4, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row4, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row4, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *label_uvc = lv_label_create(row4);
    lv_label_set_text(label_uvc, trans.tr("UVC").c_str());
    sw_uvc = lv_switch_create(row4);
    lv_obj_add_state(sw_uvc, is_usb_uvc_on() ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sw_uvc, [](lv_event_t *e) {
        uint32_t code = lv_event_get_code(e);
        if (code == LV_EVENT_VALUE_CHANGED)
        {
            lv_obj_t *sw = (lv_obj_t*)lv_event_get_target(e);
            bool uvc_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
            // stop running, can NOT work for next start unless restart system
            static lv_obj_t *label_uvc_need_system_restart = NULL;
            if (!uvc_on && is_usb_uvc_on() && !label_uvc_need_system_restart)
            {
                label_uvc_need_system_restart = lv_label_create(lv_obj_get_parent(sw));
                lv_label_set_text(label_uvc_need_system_restart, trans.tr("Need reboot after turn on again!").c_str());
                // 模糊，不显眼
                // lv_obj_set_style_text_color(label_uvc_need_system_restart, lv_color_hex(0xFF0000), 0);
            }
            else if (is_usb_uvc_on() && uvc_on && label_uvc_need_system_restart)
            {
                lv_obj_del(label_uvc_need_system_restart);
                label_uvc_need_system_restart = NULL;
            }
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // create a confirm button
    lv_obj_t *btn = lv_btn_create(container);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 100);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, trans.tr("Confirm").c_str());
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        uint32_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED)
        {
            bool ncm_on = lv_obj_has_state(sw_ncm, LV_STATE_CHECKED);
            bool rndis_on = lv_obj_has_state(sw_rndis, LV_STATE_CHECKED);
            bool mouse_on = lv_obj_has_state(sw_mouse, LV_STATE_CHECKED);
            bool keyboard_on = lv_obj_has_state(sw_keyboard, LV_STATE_CHECKED);
            bool touchpad_on = lv_obj_has_state(sw_touchpad, LV_STATE_CHECKED);
            bool uvc_on = lv_obj_has_state(sw_uvc, LV_STATE_CHECKED);
            // lv_obj_t *btn = lv_event_get_current_target(e);
            uint16_t idx = lv_dropdown_get_selected(ddlist);
            log::info("set usb mode to %s mode\n", idx == 0 ? "device" : "host");
            log::info("set NIC mode: ncm: %d, rndis: %d", ncm_on, rndis_on);
            if(idx == 0)
                set_usb_mode_dev();
            else
                set_usb_mode_host();
            set_ncm(ncm_on);
            set_rndis(rndis_on);
            set_mouse(mouse_on);
            set_keyboard(keyboard_on);
            set_touchpad(touchpad_on);
            set_uvc(uvc_on);
            restart_usb();

            // show toast
            static char buff[12];
            static char *btns[2] = {buff, (char*)""};
            // copy trans.tr("OK").c_str() to btns[0]

            const char *c = trans.tr("OK").c_str();
            size_t len = strlen(c);
            strncpy(btns[0], c, len >= sizeof(buff) ? sizeof(buff) - 1 : len);
            btns[0][len] = '\0';
            lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
            lv_msgbox_add_title(mbox, "");
            lv_msgbox_add_text(mbox, trans.tr("Set complete").c_str());
            lv_msgbox_add_close_button(mbox);
            lv_obj_t *btn = lv_msgbox_add_footer_button(mbox, btns[0]);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
                lv_msgbox_close(mbox);
            }, LV_EVENT_CLICKED, mbox);
            lv_obj_center(mbox);
        }
    }, LV_EVENT_CLICKED, NULL);

}

void on_usb_settings_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
}

