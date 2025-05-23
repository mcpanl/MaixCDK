#include "stdio.h"
#include "lvgl.h"
#include "ui_screen.h"
#include "ui_utils.h"
#include "ui_event_handler.h"

#define DEBUG_ENABLE
#ifdef DEBUG_ENABLE
#define DEBUG_EN(x)                                                         \
    bool g_debug_flag = x;

#define DEBUG_PRT(fmt, ...) do {                                            \
    if (g_debug_flag)                                                       \
        printf("[%s][%d]: " fmt "\r\n", __func__, __LINE__, ##__VA_ARGS__);   \
} while(0)
#else
#define DEBUG_EN(fmt, ...)
#define DEBUG_PRT(fmt, ...)
#endif

extern lv_obj_t *g_base_screen;
extern lv_obj_t *g_lower_screen;
extern lv_obj_t *g_upper_screen;
extern lv_obj_t *g_right_screen;
extern lv_obj_t *g_big_photo_screen;
extern lv_obj_t *g_small_photo_screen;
extern lv_obj_t *g_switch_left;
extern lv_obj_t *g_switch_right;
extern lv_obj_t *g_video_bar;

extern lv_obj_t *g_btn_select;
extern lv_obj_t *g_btn_cancel;

static struct {
    unsigned int exit_flag : 1;
    unsigned int bulk_delete_flag : 1;
    unsigned int touch_small_image : 1;
    unsigned int touch_bulk_delete : 1;
    unsigned int touch_bulk_delete_cancel : 1;
    unsigned int touch_delete_big_photo : 1;
    unsigned int touch_video : 1;
    unsigned int touch_show_big_photo_info : 1;
    unsigned int touch_show_left_big_photo : 1;
    unsigned int touch_show_right_big_photo : 1;

    unsigned int touch_video_bar_press : 1;
    unsigned int touch_video_bar_release : 1;

    unsigned int touch_video_view_pressed_flag : 1;

    char *touch_small_image_dir_name;
    char *touch_small_image_photo_path;
} priv;

void event_touch_exit_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        DEBUG_PRT("try to exit\n");
        if (ui_get_view_flag()) {
            ui_set_view_flag(0);
        } else {
            priv.exit_flag = 1;
        }
    }
}

void event_touch_small_image_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    static uint8_t ignore_after_long_press = 0;
    if (code == LV_EVENT_SHORT_CLICKED) {
        if (ignore_after_long_press) {
            ignore_after_long_press = 0;
            return;
        }
        ui_photo_t *data = (ui_photo_t *)lv_event_get_user_data(e);
        if (data) {
            DEBUG_PRT("short click path:%s\n", data->path);
            if (ui_get_need_bulk_delete()) {
                data->is_touch ^= 1;
                ui_photo_list_screen_update();
            } else {
                priv.touch_small_image = 1;
                priv.touch_small_image_photo_path = data->path;
                priv.touch_small_image_dir_name = (char *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
                priv.touch_video = data->is_video;
                DEBUG_PRT("touch image, dir:%s path:%s is_video:%d\n", priv.touch_small_image_dir_name, priv.touch_small_image_photo_path, priv.touch_video);
            }
        } else {
            DEBUG_PRT("unknow error!\r\n");
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        ui_photo_t *data = (ui_photo_t *)lv_event_get_user_data(e);
        if (data) {
            DEBUG_PRT("long pressed path:%s\n", data->path);
            ignore_after_long_press = 1;
            ui_set_need_bulk_delete(1);
            ui_photo_list_screen_update();
            if (g_lower_screen) {
                lv_obj_remove_flag(g_lower_screen, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(g_btn_cancel, LV_OBJ_FLAG_HIDDEN);
            	lv_obj_add_flag(g_btn_select, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            DEBUG_PRT("unknow error!\r\n");
        }
    }
}


void event_touch_select_btn_cb(lv_event_t * e)
{
	DEBUG_EN(1);
	static uint8_t ignore_after_long_press = 0;	
	ignore_after_long_press = 1;
        ui_set_need_bulk_delete(1);
        ui_photo_list_screen_update();
        if (g_lower_screen) {
            lv_obj_remove_flag(g_lower_screen, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(g_btn_cancel, LV_OBJ_FLAG_HIDDEN);
	    lv_obj_add_flag(g_btn_select, LV_OBJ_FLAG_HIDDEN);
	}
	
	DEBUG_PRT("SELECT BTN\r\n");
}

void event_touch_cancel_btn_cb(lv_event_t * e)
{
	DEBUG_EN(1);
        
	priv.touch_bulk_delete_cancel = 1;
        ui_set_need_bulk_delete(0);
        ui_photo_list_screen_update();
        ui_photo_clear_all_photo_flag();
        if (g_lower_screen) {
            lv_obj_add_flag(g_lower_screen, LV_OBJ_FLAG_HIDDEN);
       	    lv_obj_add_flag(g_btn_cancel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(g_btn_select, LV_OBJ_FLAG_HIDDEN);
	}

	DEBUG_PRT("CANCEL BTN\r\n");
}

void event_touch_big_image_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        ui_big_photo_info_t *data = (ui_big_photo_info_t *)lv_event_get_user_data(e);
        if (data) {
            DEBUG_PRT("short click path:%s\n", data->path);
            ui_set_view_flag(3);
        } else {
            DEBUG_PRT("unknow error!\r\n");
        }
    }
}

void event_touch_show_left_big_photo_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        DEBUG_PRT("touch left\n");
        priv.touch_show_left_big_photo = 1;
    } else {
        DEBUG_PRT("unknow error!\r\n");
    }
}

void event_touch_show_right_big_photo_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) {
        DEBUG_PRT("touch right\n");
        priv.touch_show_right_big_photo = 1;
    } else {
        DEBUG_PRT("unknow error!\r\n");
    }
}

static void confirm_btn_event_cb(lv_event_t * e) {
	lv_obj_remove_flag(g_lower_screen, LV_OBJ_FLAG_HIDDEN);
    const char * action = lv_event_get_user_data(e);
    lv_obj_t * dialog = lv_obj_get_parent(lv_event_get_target(e));

    if (strcmp(action, "delete_big_photo") == 0) {
        priv.touch_delete_big_photo = 1;
    } else if (strcmp(action, "bulk_delete") == 0) {
        priv.touch_bulk_delete = 1;
    }

    lv_obj_del(dialog);
}

static void cancel_btn_event_cb(lv_event_t * e) {
	lv_obj_remove_flag(g_lower_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_del(lv_obj_get_parent(lv_event_get_target(e))); // 删除弹窗
}

void show_confirm_dialog(const char * message, const char * action) {
	lv_obj_add_flag(g_lower_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t * dialog = lv_obj_create(lv_layer_sys());
    lv_obj_set_size(dialog, 640, 480);
    lv_obj_center(dialog);
    lv_obj_set_style_radius(dialog, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    lv_obj_move_foreground(dialog);
    lv_obj_set_style_pad_all(dialog, 10, 0);

    lv_obj_t * label = lv_label_create(dialog);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_center(label);

    // 确认按钮
    lv_obj_t * btn_ok = lv_btn_create(dialog);
    lv_obj_set_size(btn_ok, 120, 50);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -60, -90);
    lv_obj_add_event_cb(btn_ok, confirm_btn_event_cb, LV_EVENT_CLICKED, (void *)action);

    lv_obj_t * label_ok = lv_label_create(btn_ok);
    lv_label_set_text(label_ok, "Confirm");
    lv_obj_set_style_text_font(label_ok, &lv_font_montserrat_24, 0);
    lv_obj_center(label_ok);

    // 取消按钮
    lv_obj_t * btn_cancel = lv_btn_create(dialog);
    lv_obj_set_size(btn_cancel, 120, 50);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 60, -90);
    lv_obj_add_event_cb(btn_cancel, cancel_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(label_cancel, "Cancel");
    lv_obj_set_style_text_font(label_cancel, &lv_font_montserrat_24, 0);
    lv_obj_center(label_cancel);
}



void event_touch_delete_big_photo_cb(lv_event_t * e)
{
    DEBUG_EN(1);
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        DEBUG_PRT("touch delete big photo\n");
        show_confirm_dialog("Confirm to Delete?", "delete_big_photo");
    }
}


void event_touch_delete_big_photo_cb2(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        DEBUG_PRT("touch delete big photo\n");
        priv.touch_delete_big_photo = 1;
    }
}

void event_touch_show_big_photo_info_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        DEBUG_PRT("touch show big photo info\n");
        priv.touch_show_big_photo_info = 1;
        if (g_right_screen) {
            if (lv_obj_has_flag(g_right_screen, LV_OBJ_FLAG_HIDDEN)) {
                ui_right_screen_update();
                lv_obj_remove_flag(g_right_screen, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(g_right_screen, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void event_touch_bulk_delete_cb(lv_event_t * e)
{
    DEBUG_EN(1);
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        DEBUG_PRT("touch bulk delete\n");
        show_confirm_dialog("Confirm to Delete?", "bulk_delete");
    }
}

void event_touch_bulk_delete_cb2(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        DEBUG_PRT("touch bulk delete\n");
        priv.touch_bulk_delete = 1;
    }
}

void event_touch_bulk_delete_cancel_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        DEBUG_PRT("touch bulk delete cancel\n");
        priv.touch_bulk_delete_cancel = 1;
        ui_set_need_bulk_delete(0);
        ui_photo_list_screen_update();
        ui_photo_clear_all_photo_flag();
        if (g_lower_screen) {
            lv_obj_add_flag(g_lower_screen, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void event_video_bar_event_cb(lv_event_t * e)
{
    DEBUG_EN(0);
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_PRESSING) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point);

        int w = abs(obj->coords.x2 - obj->coords.x1);
        int w_oft = point.x - obj->coords.x1;
        DEBUG_PRT("cuculate x1:%d x2:%d point.x:%d w_oft:%d", obj->coords.x1, obj->coords.x2, point.x, w_oft);
        w_oft = w_oft > w ? w : w_oft;
        w_oft = w_oft < 0 ? 0 : w_oft;
        DEBUG_PRT("cuculate w:%d w_oft:%d", w, w_oft);
        lv_bar_t * bar = (lv_bar_t *)obj;
        int bar_max_val = abs(bar->max_value - bar->min_value);
        int val = bar->min_value + bar_max_val * w_oft / w;
        DEBUG_PRT("cuculate bar_max_val:%d bar_min_val:%d bar_val:%d", bar->max_value, bar->min_value, val);
        lv_bar_set_value((lv_obj_t *)bar, val, LV_ANIM_OFF);

        priv.touch_video_bar_press = 1;
        DEBUG_PRT("found bar value: %d", val);
    } else if (code == LV_EVENT_RELEASED) {
        int val = lv_bar_get_value(obj);
        DEBUG_PRT("found bar value: %d", val);

        priv.touch_video_bar_release = 1;
    }
}

void event_video_view_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        priv.touch_video_view_pressed_flag = 1;
    }
}


bool ui_get_exit_flag(void)
{
    bool ret = priv.exit_flag;
    priv.exit_flag = 0;
    return ret;
}

bool ui_touch_is_video_image_flag(void)
{
    return priv.touch_video;
}

bool ui_get_touch_small_image_flag(void)
{
    bool ret = priv.touch_small_image;
    priv.touch_small_image = 0;
    return ret;
}

void ui_get_touch_small_image_path(char **dir_name, char **img_path)
{
    if (dir_name) {
        *dir_name = priv.touch_small_image_dir_name;
    }

    if (img_path) {
        *img_path = priv.touch_small_image_photo_path;
    }
}

bool ui_get_bulk_delete_flag(void)
{
    bool ret = priv.touch_bulk_delete;
    priv.touch_bulk_delete = 0;
    return ret;
}

bool ui_get_bulk_delete_cancel_flag(void)
{
    bool ret = priv.touch_bulk_delete_cancel;
    priv.touch_bulk_delete_cancel = 0;
    return ret;
}

bool ui_get_show_big_photo_info_flag(void)
{
    bool ret = priv.touch_show_big_photo_info;
    priv.touch_show_big_photo_info = 0;
    return ret;
}

bool ui_get_delete_big_photo_flag(void)
{
    bool ret = priv.touch_delete_big_photo;
    priv.touch_delete_big_photo = 0;
    return ret;
}

char *ui_get_delete_big_photo_path(void)
{
    ui_big_photo_info_t *info = (ui_big_photo_info_t *)lv_obj_get_user_data(g_big_photo_screen);
    return info->path;
}

bool ui_get_touch_show_right_big_photo_flag(void)
{
    bool ret = priv.touch_show_right_big_photo;
    priv.touch_show_right_big_photo = 0;
    return ret;
}

bool ui_get_touch_show_left_big_photo_flag(void)
{
    bool ret = priv.touch_show_left_big_photo;
    priv.touch_show_left_big_photo = 0;
    return ret;
}

bool ui_get_touch_video_bar_press_flag(void)
{
    bool ret = priv.touch_video_bar_press;
    priv.touch_video_bar_press = 0;
    return ret;
}

bool ui_get_touch_video_bar_release_flag(void)
{
    bool ret = priv.touch_video_bar_release;
    priv.touch_video_bar_release = 0;
    return ret;
}

double ui_get_video_bar_value(void)
{
    DEBUG_EN(0);
    if (g_video_bar) {
        lv_bar_t * bar = (lv_bar_t *)g_video_bar;
        int bar_max_val = abs(bar->max_value - bar->min_value);
        double value = lv_bar_get_value((lv_obj_t *)bar);
        value = value / bar_max_val;
        DEBUG_PRT("cuculate bar_max_val:%d value:%f", bar_max_val, value);
        return value;
    }

    return 0;
}

bool ui_get_video_view_pressed_flag(void)
{
    bool ret = priv.touch_video_view_pressed_flag;
    priv.touch_video_view_pressed_flag = 0;
    return ret;
}
