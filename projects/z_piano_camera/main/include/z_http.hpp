#pragma once
#include "httplib.h"
#include "json.hpp"
#include <iostream>
#include <thread>

namespace z {
    class Http {
    public:
        Http();

        ~Http();
    private:
        int http_port = 8080;
        httplib::Server *server;
        std::thread server_thread;
        void start();
        void stop();
        void register_route();

        void network_monitor();   // 检测网络线程

        std::thread network_monitor_thread;
        std::atomic<bool> server_running {false};
        std::atomic<bool> stop_flag {false};
        std::mutex server_mutex;
    };
}
