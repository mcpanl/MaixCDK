#include "z_network.hpp"

#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "../../../../components/basic/include/maix_app.hpp"


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

        // 线程1：初始化网卡
        std::thread([this]() {
            this->init_network();
        }).detach();

        // 线程2：周期性检测连通性
        monitor_thread_ = std::thread([this]() {
            this->monitor_network();
        });

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

        if (monitor_thread_.joinable()) {
            monitor_thread_.detach();  // 或者设置标志退出循环
        }

        delete wifi_;
    }

    void Network::update_network_info() {
        auto wifi_ifaces = wifi::list_devices();
        if (!wifi_ifaces.empty()) {
            // log::info("wifi iface: %s", wifi_ifaces[0].c_str());
            wifi_ = new wifi::Wifi(wifi_ifaces[0]);

            std::string netmask = guess_netmask(wifi_->get_ip());

            // log::info("IP: %s", wifi_->get_ip().c_str());
            // log::info("MAC: %s", wifi_->get_mac().c_str());
            // log::info("Gateway: %s", wifi_->get_gateway().c_str());
            // log::info("MASK: %s", netmask.c_str());

            {
                std::lock_guard<std::mutex> lock(mutex_);
                ip_addr_ = wifi_->get_ip();
                ip_addr_binary_ = inet_addr(ip_addr_.c_str());
                card_state_ = NetCardState::UP;
            }
        }
    }

    void Network::init_network() {
        for (int i = 0; i < 30; ++i) {
            auto wifi_ifaces = wifi::list_devices();
            if (!wifi_ifaces.empty()) {
                log::info("wifi iface: %s", wifi_ifaces[0].c_str());
                wifi_ = new wifi::Wifi(wifi_ifaces[0]);

                std::string netmask = guess_netmask(wifi_->get_ip());

                log::info("IP: %s", wifi_->get_ip().c_str());
                log::info("MAC: %s", wifi_->get_mac().c_str());
                log::info("Gateway: %s", wifi_->get_gateway().c_str());
                log::info("MASK: %s", netmask.c_str());

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ip_addr_ = wifi_->get_ip();
                    ip_addr_binary_ = inet_addr(ip_addr_.c_str());
                    card_state_ = NetCardState::UP;
                }
                return;
            }

            log::info("network not ready, wait 1s...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        log::error(">>>> no wifi iface found after timeout <<<<");
    }

    void Network::monitor_network() {
        while (!app::need_exit()) {
            update_network_info();
            if (card_state_ == NetCardState::UP && wifi_ != nullptr) {
                // std::string gateway = wifi_->get_gateway();
                // if (!gateway.empty() && ping(gateway)) {
                //     printf(">>> local ok\n");
                //     lan_state_ = LanState::CONNECTED;
                // } else {
                //     printf(">>> local ng\n");
                //     lan_state_ = LanState::DISCONNECTED;
                // }

                // 访问外网
                if (ping("8.8.8.8")) {
                    // printf(">>> network ok\n");
                    ping_fail_count_ = 0;
                    lan_state_ = LanState::CONNECTED;
                    wan_state_ = WanState::CONNECTED;
                } else {
                    // printf(">>> network ng\n");
                    ping_fail_count_++;
                    if (ping_fail_count_ >= 3) {
                        lan_state_ = LanState::DISCONNECTED;
                        wan_state_ = WanState::DISCONNECTED;
                    }
                }
            } else {
                lan_state_ = LanState::DISCONNECTED;
                wan_state_ = WanState::DISCONNECTED;
            }

            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    bool Network::ping(const std::string& addr) {
        std::string cmd = "ping -c 1 -W 2 " + addr + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return false;

        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            // 如果不想输出，就什么都不做
        }

        int status = pclose(pipe);
        return (status == 0);
    }

    std::string Network::get_ip_addr() {
        return ip_addr_;
    }

    in_addr_t Network::get_ip_addr_binary() {
        return ip_addr_binary_;
    }

    NetCardState Network::get_card_state() {
        return card_state_.load();
    }

    LanState Network::get_lan_state() {
        return lan_state_.load();
    }

    WanState Network::get_wan_state() {
        return wan_state_.load();
    }
}
