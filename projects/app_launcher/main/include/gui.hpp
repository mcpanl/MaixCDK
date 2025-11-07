#pragma once

#include "maix_gui.hpp"
#include "maix_app.hpp"

namespace launcher
{
    extern void create_home_items(Maix_GUI_Activity *activity, vector<maix::app::APP_Info>& app_info, void* screen);
    extern void home_items_free(Maix_GUI_Activity *activity, vector<maix::app::APP_Info>& app_info, void* screen);
}
