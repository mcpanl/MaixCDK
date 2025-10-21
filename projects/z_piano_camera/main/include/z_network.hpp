#pragma once
#include <string>
#include <netinet/in.h>
#include "maix_wifi.hpp"
#include "maix_network.hpp"
#include "maix_basic.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace z {
    enum class NetCardState {
        DOWN,
        UP
    };

    enum class LanState {
        DISCONNECTED,
        CONNECTED
    };

    enum class WanState {
        DISCONNECTED,
        CONNECTED
    };

    class Network {
        public:
        Network();
        ~Network();

        NetCardState get_card_state();
        LanState get_lan_state();
        WanState get_wan_state();

        std::string get_ip_addr();
        in_addr_t get_ip_addr_binary();
    private:
        maix::network::wifi::Wifi* wifi_;
        std::string ip_addr_;
        in_addr_t ip_addr_binary_;

    private:
        void init_network();
        void monitor_network();
        void update_network_info();

        bool ping(const std::string& addr);

        std::atomic<NetCardState> card_state_{NetCardState::DOWN};
        std::atomic<LanState> lan_state_{LanState::DISCONNECTED};
        std::atomic<WanState> wan_state_{WanState::DISCONNECTED};

        std::mutex mutex_;
        std::thread monitor_thread_;
    };
}
