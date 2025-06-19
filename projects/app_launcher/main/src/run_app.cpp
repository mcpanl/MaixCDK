
#include "run_app.hpp"
#include "global_config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "maix_fs.hpp"
#include "maix_basic.hpp"

using namespace maix;

char* launcher::g_app_exec_path = NULL;

void launcher::run_app(const char *app_path, const char* launcher_exec, const char *app_id)
{
    FILE *file;

    file = fopen("/tmp/run_app.txt", "w");
    if (file == NULL)
    {
        perror("\nopen /tmp/run_app.txt failed!!\n");
        return;
    }
    fputs(app_path, file); // first line: app exec path
    fputs("\n", file);
    fputs(app_id, file);   // second line: app id
    fputs("\n", file);
    fputs("", file);       // third line: start_param, default empty launched by launcher
    fputs("\n", file);
    fclose(file);
    app::set_exit_flag(true);
}
