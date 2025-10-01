#include "z_key.hpp"
#include "priv.hpp"
using namespace maix;
using namespace maix::peripheral;

namespace z {

    Key* Key::instance = nullptr;

    void Key::on_key(int key_code, int state)
    {
        if (instance) {
            instance->handle_key(key_code, state);
        }
    }

    void Key::handle_key(int key_code, int state)
    {
        log::info("key: %d, state: %d\n", key_code, state);
        if (key_code != 352) {
            if (key_code == 116) {
                if (priv.display) {
                    if (state == 0) {
                        priv.display->toggle_backlight();
                    }
                }
            }
            return;
        }

        if (state == 1 || state == 2) { // 按下
            if (key_stage == SHORT_PRESSED) {
                auto now = std::chrono::steady_clock::now();
                int delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - short_press_time).count();
                if (delta <= 1200) {
                    press_start = now;
                    countdown_ms = 3800;
                    key_stage = LONG_PRESSING;
                } else {
                    key_stage = IDLE;
                }
            }
        } else { // 松开
            if (key_stage == IDLE) {
                short_press_time = std::chrono::steady_clock::now();
                key_stage = SHORT_PRESSED;
            } else if (key_stage == LONG_PRESSING) {
                key_stage = IDLE;
                countdown_ms = 0;
            }
        }

        last_key = key_code;
        last_state = state;
    }

    Key::Key()
        : key(key::Key(on_key)) // ✅ 这里用你要的方式初始化
    {
        printf("Key::Key()\n");
        instance = this;
    }

    Key::~Key() {
        printf("Key::~Key()\n");
        instance = nullptr;
    }

    KeyStage Key::get_stage() const {
        return key_stage.load();
    }

    int Key::get_long_press_ms() const {
        if (key_stage != LONG_PRESSING) {
            return 0;
        }
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - press_start).count();
    }
}
