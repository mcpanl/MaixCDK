#pragma once

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <queue>
#include <functional>
#include <memory>
#include <string>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
namespace z {

    constexpr int PORT = 8060;  // 你可以根据需要修改

    class TcpServer {
    public:
        TcpServer();
        ~TcpServer();

        void start();
        void stop();

        void setMessageCallback(std::function<void(int, const std::vector<char>&)> callback);

        void broadcastText(const std::string& message);
        void broadcastBinary(const std::vector<char>& data);

    private:
        struct ClientHandler {
            int fd;
            std::thread readThread;
            std::thread writeThread;

            std::queue<std::vector<char>> sendQueue;
            std::mutex queueMutex;
            std::condition_variable queueCond;
            std::atomic<bool> active;
            std::function<void(int, const std::vector<char>&)> onMessage;
            std::function<void(int)> onDisconnect;

            ClientHandler(int client_fd, std::function<void(int, const std::vector<char>&)> messageCallback, std::function<void(int)> disconnectCallback);
            void start();
            void stop();
            void sendData(const std::vector<char>& data);

        private:
            void readLoop();
            void writeLoop();
        };

        int server_fd;
        std::thread server_thread;
        std::unordered_map<int, std::shared_ptr<ClientHandler>> clients;
        std::mutex client_mutex;
        std::atomic<bool> running;

        std::function<void(int, const std::vector<char>&)> onMessage;

        void run();
    };

} // namespace z
