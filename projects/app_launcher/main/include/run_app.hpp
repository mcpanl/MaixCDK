#pragma once


namespace launcher
{
    extern char* g_app_exec_path;
    void run_app(const char* exec_path, const char* launcher_exec, const char* app_id);
}
