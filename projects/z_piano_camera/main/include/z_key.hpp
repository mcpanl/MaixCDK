#pragma once
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "maix_basic.hpp"
#include "maix_key.hpp"

namespace z {
    // 状态机
    enum KeyStage {
        IDLE = 0,
        SHORT_PRESSED,
        LONG_PRESSING
    };

    class Key {
    public:
        Key();
        ~Key();

        // 对外暴露接口
        KeyStage get_stage() const;
        int get_long_press_ms() const;

    private:
        // 内部使用的回调（静态函数）
        static void on_key(int key, int state);

        // 实际处理逻辑（实例方法）
        void handle_key(int key, int state);

        // maix 的按键对象（注意：不是 z::Key，而是 maix::peripheral::key::Key）
        maix::peripheral::key::Key key;

        // 状态相关
        std::atomic<KeyStage> key_stage{IDLE};
        std::atomic<int> countdown_ms{0};
        std::chrono::steady_clock::time_point short_press_time;
        std::chrono::steady_clock::time_point press_start;

        int last_key{0};
        int last_state{0};

        // 保存实例指针（静态转发用）
        static Key* instance;
    };
}
