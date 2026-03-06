#include "z_tcp_server.hpp"
#include <cerrno>
#include <cstring>
#include <chrono>

#include "maix_basic.hpp"

using namespace maix;

namespace z {

// ================= TcpServer =================
TcpServer::TcpServer() : server_fd(-1), running(false) {
    printf("==== TcpServer ====\n");
}

TcpServer::~TcpServer() {
    printf("~~~~ TcpServer ~~~~\n");
    stop();
}

void TcpServer::start() {
    running = true;
    server_thread = std::thread(&TcpServer::run, this);
}

void TcpServer::stop() {
    running = false;

    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }

    {
        std::lock_guard<std::mutex> lock(client_mutex);
        for (auto& [fd, client] : clients) {
            printf("    **** wait client stop...\n");
            client->stop();
        }
        clients.clear();
    }
    
    if (server_thread.joinable()) server_thread.join();
}

void TcpServer::setMessageCallback(std::function<void(int, const std::vector<char>&)> callback) {
    onMessage = callback;
}

void TcpServer::broadcastText(const std::string& message) {
    std::vector<char> data(message.begin(), message.end());
    broadcastBinary(data);
}

void TcpServer::broadcastBinary(const std::vector<char>& data) {
    std::lock_guard<std::mutex> lock(client_mutex);
    for (auto& [fd, client] : clients) {
        client->sendData(data);
    }
}

void TcpServer::run() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Socket creation failed\n";
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        close(server_fd);
        return;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return;
    }

    std::cout << "Server listening on port " << PORT << "...\n";

    while (!app::need_exit() && running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            std::cout << "New client connected: " << client_fd << "\n";

            auto client = std::make_shared<ClientHandler>(
                client_fd,
                onMessage,
                [this](int fd) {
                    std::lock_guard<std::mutex> lock(client_mutex);
                    auto it = clients.find(fd);
                    if (it != clients.end()) {
                        // it->second->stop();
                        // clients.erase(it);
                        std::cout << "Client removed from server: " << fd << std::endl;
                    }
                }
            );

            {
                std::lock_guard<std::mutex> lock(client_mutex);
                clients[client_fd] = client;
            }
            client->start();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(125));
        }
    }

    std::cout << "Server stopped accepting new clients.\n";
}

// ================= ClientHandler =================
TcpServer::ClientHandler::ClientHandler(
    int client_fd,
    std::function<void(int, const std::vector<char>&)> messageCallback,
    std::function<void(int)> disconnectCallback
)
    : fd(client_fd),
      active(true),
      onMessage(std::move(messageCallback)),
      onDisconnect(std::move(disconnectCallback))
{}

void TcpServer::ClientHandler::start() {
    readThread = std::thread(&ClientHandler::readLoop, this);
    writeThread = std::thread(&ClientHandler::writeLoop, this);
}

void TcpServer::ClientHandler::stop() {
    printf("**** CLIENT HANDLER STOP ****\n");
    active = false;
    shutdown(fd, SHUT_RDWR);
    queueCond.notify_all();

    auto self_id = std::this_thread::get_id();
    if (readThread.joinable() && readThread.get_id() != self_id) {
        // readThread.join();
    }
    if (writeThread.joinable() && writeThread.get_id() != self_id) {
        // writeThread.join();
    }

    close(fd);
}

void TcpServer::ClientHandler::sendData(const std::vector<char>& data) {
    if (!active) return;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        // printf("ClientHandler try send data \n");
        sendQueue.push(data);
    }
    queueCond.notify_one();
}

void TcpServer::ClientHandler::readLoop() {
    while (!app::need_exit() && active) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        int ret = select(fd + 1, &read_fds, nullptr, nullptr, nullptr);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[readLoop] select error\n";
            break;
        }

        if (FD_ISSET(fd, &read_fds)) {
            char buffer[1024];
            ssize_t bytes_read = recv(fd, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) {
                std::cout << "Client disconnected: " << fd << std::endl;

                if (onDisconnect) {   // 👈 调用回调，通知 TcpServer 删除自己
                    onDisconnect(fd);
                }
                break;
            }

            if (onMessage) {
                std::vector<char> data(buffer, buffer + bytes_read);
                onMessage(fd, data);
            }
        }
    }

    active = false;
    queueCond.notify_all();
}

void TcpServer::ClientHandler::writeLoop() {
    while (!app::need_exit() && active) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCond.wait(lock, [this]() {
            return !sendQueue.empty() || !active || app::need_exit();
        });

        while (!sendQueue.empty() && active && !app::need_exit()) {
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(fd, &write_fds);

            lock.unlock();
            int ret = select(fd + 1, nullptr, &write_fds, nullptr, nullptr);
            lock.lock();

            if (ret < 0) {
                if (errno == EINTR) continue;
                std::cerr << "[writeLoop] select error\n";
                break;
            }

            if (FD_ISSET(fd, &write_fds)) {
                auto msg = sendQueue.front();
                ssize_t sent = send(fd, msg.data(), msg.size(), 0);
                if (sent < 0) {
                    std::cerr << "Send error on client: " << fd << std::endl;
                    active = false;
                    break;
                }
                sendQueue.pop();
            }
        }
    }
}

} // namespace z
