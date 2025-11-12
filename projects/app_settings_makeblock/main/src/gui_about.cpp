#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_thread.hpp"
#include "maix_app.hpp"
#include "maix_gui.hpp"
#include "maix_time.hpp"
#include "maix_log.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>


void on_about_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
	lv_obj_t *root = (lv_obj_t *)obj;
	lv_obj_t *container = lv_obj_create(root);
	lv_obj_set_style_radius(container, 0, LV_PART_MAIN);
	lv_obj_set_size(container, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(container, theme_bg_color, LV_PART_MAIN);
	lv_obj_set_style_text_color(container, theme_text_color, LV_PART_MAIN);
	lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
	
	// 垂直布局
	lv_obj_set_layout(container, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	
	// 启用仅垂直滚动
	lv_obj_set_scroll_dir(container, LV_DIR_VER);
	
	lv_obj_t *label = lv_label_create(container);
	
	// 设置最大宽度为屏幕宽度的 80%
	lv_obj_set_width(label, lv_pct(80));
	
	// 启用自动换行
	lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

	lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);

	lv_obj_set_style_text_line_space(label, 8, LV_PART_MAIN);

	// 设置文本
	lv_label_set_text(label,
	    "Thank you for choosing our [AI camera product]. Please carefully review the following terms before proceeding: \n"
	    "Data Collection and Local Processing \n"
	    "By proceeding, you are deemed to have consented to the collection and processing of certain biometric and behavioral data, including: \n"
	    "Facial recognition - Capturing facial features for identification and personalized experience. \n"
	    "Voice and speech recognition - Processing spoken input for interaction and command execution. \n"
	    "Gesture and behavior recognition - Analyzing body movements to enhance functionality. \n"
	    "This product operates in local mode by default: \n"
	    "All data is exclusively stored and controlled on your local device - we do not access, collect, or process any locally stored data."
	);
}

void on_about_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
}
