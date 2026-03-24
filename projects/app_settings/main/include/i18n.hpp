#pragma once

#include "maix_i18n.hpp"
#include <map>
#include <vector>
#include "ui.h"
#include "lvgl.h"

using namespace maix;
using namespace std;

extern i18n::Trans trans;

extern const lv_font_t *get_font_by_locale(const string &locale);

#define _(key) trans.tr(key).c_str()
