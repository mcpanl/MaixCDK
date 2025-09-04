#include "z_network.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>



using namespace maix;
using namespace maix::network;

std::string guess_netmask(const std::string& ip)
{
    if(ip.empty()) return "255.255.255.0"; // fallback

    int first_octet = std::stoi(ip.substr(0, ip.find('.')));
    int second_octet = std::stoi(ip.substr(ip.find('.') + 1, ip.find('.', ip.find('.') + 1)));

    if(first_octet == 10)
        return "255.0.0.0";
    else if(first_octet == 172 && (second_octet >= 16 && second_octet <= 31))
        return "255.240.0.0";
    else
        return "255.255.255.0"; // assume 192.168.x.x
}


namespace z {
    Network::Network() {
        printf("==== Network ====\n");
        std::vector<std::string> wifi_ifaces = wifi::list_devices();
        for(auto &iface : wifi_ifaces)
        {
            log::info("wifi iface: %s", iface.c_str());
        }
        if(wifi_ifaces.empty())
        {
            log::error(">>>> no wifi iface found <<<<");
        } else {
            wifi_ = new wifi::Wifi(wifi_ifaces[0]);

            std::string netmask = guess_netmask(wifi_->get_ip());

            log::info("IP: %s", wifi_->get_ip().c_str());
            log::info("MAC: %s", wifi_->get_mac().c_str());
            log::info("Gateway: %s", wifi_->get_gateway().c_str());
            log::info("MASK: %s", netmask.c_str());
        }

        ip_addr_ = wifi_->get_ip();
        ip_addr_binary_ = inet_addr(ip_addr_.c_str());

    }

    Network::~Network() {
        printf("~~~~ Network ~~~~\n");
        delete wifi_;
    }

    std::string Network::get_ip_addr() {
        return ip_addr_;
    }

    in_addr_t Network::get_ip_addr_binary() {
        return ip_addr_binary_;
    }


}
