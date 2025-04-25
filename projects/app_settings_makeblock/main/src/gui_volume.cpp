#include "main.hpp"
#include <string>
#include "lvgl.h"
#include "maix_thread.hpp"
#include "maix_app.hpp"
#include "maix_gui.hpp"
#include "maix_time.hpp"
#include "maix_log.hpp"
#include "maix_audio.hpp"

#include "i18n.hpp"
#include "main.hpp"
#include <vector>
#include "ui.h"
#include <string.h>

using namespace maix;
static void slider_event_cb(lv_event_t *e);
static lv_obj_t *speaker_slider_label = NULL, *microphone_slider_label = NULL;

static void set_speaker_volume(int volume) {
    auto speaker = maix::audio::Player();
    speaker.volume(volume);
}

static void set_microphone_volume(int volume) {
    auto mic = maix::audio::Recorder();
    mic.volume(volume);
}

static void speaker_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    char buf[8];
    int value = (int)lv_slider_get_value(slider);
    if(value < 5)
        value = 5;
    lv_snprintf(buf, sizeof(buf), "%d%%", value);
    lv_label_set_text(speaker_slider_label, buf);
    set_speaker_volume(value);
    app::set_sys_config_kv("volume", "speaker_volume", std::to_string(value));
}

static void microphone_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    char buf[8];
    int value = (int)lv_slider_get_value(slider);
    if(value < 5)
        value = 5;
    lv_snprintf(buf, sizeof(buf), "%d%%", value);
    lv_label_set_text(microphone_slider_label, buf);
    set_microphone_volume(value);
    app::set_sys_config_kv("volume", "microphone_volume", std::to_string(value));
}

void on_volume_gui(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
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
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // speaker
    lv_obj_t *speaker_label = lv_label_create(container);
    lv_label_set_text(speaker_label, _("Speaker"));
    lv_obj_t *speaker_slider = lv_slider_create(container);
    lv_obj_set_style_anim_duration(speaker_slider, 2000, 0);
    lv_obj_add_event_cb(speaker_slider, speaker_slider_event_cb, LV_EVENT_RELEASED, NULL);
    speaker_slider_label = lv_label_create(container);

    // micorphone
    lv_obj_t *microphone_label = lv_label_create(container);
    lv_label_set_text(microphone_label, _("Microphone"));
    lv_obj_t *microphone_slider = lv_slider_create(container);
    lv_obj_set_style_anim_duration(microphone_slider, 2000, 0);
    lv_obj_add_event_cb(microphone_slider, microphone_slider_event_cb, LV_EVENT_RELEASED, NULL);
    microphone_slider_label = lv_label_create(container);

    // init status
    std::string speaker_value_str = app::get_sys_config_kv("volume", "speaker_volume");
    int speaker_value = 80;
    try
    {
        if(!speaker_value_str.empty())
            speaker_value = atoi(speaker_value_str.c_str());
    }
    catch(...)
    {
        speaker_value = 80;
    }
    lv_slider_set_value(speaker_slider, int(speaker_value), LV_ANIM_OFF);
    lv_label_set_text(speaker_slider_label, (std::to_string((int)speaker_value) + "%").c_str());

    std::string microphone_value_str = app::get_sys_config_kv("volume", "microphone_volume");
    int microphone_value = 80;
    try
    {
        if(!microphone_value_str.empty())
            microphone_value = atoi(microphone_value_str.c_str());
    }
    catch(...)
    {
        microphone_value = 80;
    }
    lv_slider_set_value(microphone_slider, int(microphone_value), LV_ANIM_OFF);
    lv_label_set_text(microphone_slider_label, (std::to_string((int)microphone_value) + "%").c_str());
}

void on_volume_gui_destroy(Maix_GUI_Activity *activity, void *obj, Maix_Activity_MSG *msg, void *args)
{
}
