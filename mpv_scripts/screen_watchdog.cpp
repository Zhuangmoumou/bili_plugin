// screen_watchdog.cpp
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/inotify.h>

static const std::string SCREEN_STATUS_PATH = "/tmp/screen_status";
static const std::string WATCH_DIR = "/tmp";

class ScreenWatchdog {
public:
    void run() {
        std::cout << "[INFO] Watching directory: " << WATCH_DIR << std::endl;

        int fd = inotify_init1(IN_NONBLOCK);
        if (fd < 0) {
            perror("inotify_init1");
            return;
        }

        int wd = inotify_add_watch(fd, WATCH_DIR.c_str(), IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_CLOSE_WRITE);
        if (wd < 0) {
            perror("inotify_add_watch");
            close(fd);
            return;
        }

        char buffer[4096]
            __attribute__((aligned(__alignof__(struct inotify_event))));

        while (running) {
            ssize_t len = read(fd, buffer, sizeof(buffer));

            if (len <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            const char *ptr = buffer;

            while (ptr < buffer + len) {
                const struct inotify_event *event =
                    reinterpret_cast<const struct inotify_event *>(ptr);

                if (event->len > 0) {
                    std::string name(event->name);

                    if (name == "screen_status") {
                        if (event->mask & (IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE)) {
                            handle_new_creation();
                        }

                        if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                            reset_trigger();
                        }
                    }
                }

                ptr += sizeof(struct inotify_event) + event->len;
            }
        }

        inotify_rm_watch(fd, wd);
        close(fd);
    }

private:
    std::atomic<bool> triggered{false};
    std::atomic<int> generation{0};
    std::atomic<bool> running{true};

    void reset_trigger() {
        triggered = false;
        ++generation;
        std::cout << "[INFO] screen_status removed, trigger reset" << std::endl;
    }

    void handle_new_creation() {
        if (triggered) {
            std::cout << "[INFO] screen_status already handled, ignoring" << std::endl;
            return;
        }

        int current_gen = ++generation;

        std::thread([this, current_gen]() {
            // 等效 Timer(0.2, check_and_trigger)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            check_and_trigger(current_gen);
        }).detach();
    }

    void check_and_trigger(int gen) {
        if (access(SCREEN_STATUS_PATH.c_str(), F_OK) != 0) {
            return;
        }

        triggered = true;

        int current_gen = ++generation;

        std::thread([this, current_gen]() {
            // 等效 Timer(2.1, call_ubus)
            std::this_thread::sleep_for(std::chrono::milliseconds(2100));

            // 如果中途被新的事件取消或状态文件已不存在，则不执行
            if (generation == current_gen && access(SCREEN_STATUS_PATH.c_str(), F_OK) == 0) {
                call_ubus();
            }
        }).detach();

        std::cout << "[INFO] Detected NEW screen_status, scheduled ubus call in 2s" << std::endl;
    }

    void call_ubus() {
        std::cout << "[INFO] Calling ubus..." << std::endl;

        int ret = std::system(
            "ubus call eq_drc_process.output.rpc control '{\"action\":\"Open\"}'"
        );

        if (ret == 0) {
            std::cout << "[INFO] ubus call executed." << std::endl;
        } else {
            std::cout << "[ERROR] ubus call failed: " << ret << std::endl;
        }
    }
};

int main() {
    ScreenWatchdog watchdog;
    watchdog.run();
    return 0;
}
