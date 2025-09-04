#pragma once
#include <string>
#include <netinet/in.h>
#include "maix_wifi.hpp"
#include "maix_network.hpp"
#include "maix_basic.hpp"

namespace z {
    class Network {
        public:
        Network();
        ~Network();

        std::string get_ip_addr();
        in_addr_t get_ip_addr_binary();
    private:
        maix::network::wifi::Wifi* wifi_;
        std::string ip_addr_;
        in_addr_t ip_addr_binary_;
    };
}
