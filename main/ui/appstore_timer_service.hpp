#pragma once

#include "lvgl/lvgl.h"

#include <cstdint>
#include <functional>

namespace appstore_ui {

class AppStoreTimerService
{
public:
    struct Callbacks {
        std::function<void()> refresh;
        std::function<void()> package_job;
        std::function<void()> esc_hold;
        std::function<void()> sync;
    };

    explicit AppStoreTimerService(Callbacks callbacks);
    ~AppStoreTimerService();

    AppStoreTimerService(const AppStoreTimerService &) = delete;
    AppStoreTimerService &operator=(const AppStoreTimerService &) = delete;

    void start(uint32_t refresh_ms, uint32_t package_job_ms,
               uint32_t esc_hold_ms, uint32_t sync_ms);
    void stop();

private:
    static void refresh_callback(lv_timer_t *timer);
    static void package_job_callback(lv_timer_t *timer);
    static void esc_hold_callback(lv_timer_t *timer);
    static void sync_callback(lv_timer_t *timer);

    Callbacks callbacks_;
    lv_timer_t *refresh_timer_ = nullptr;
    lv_timer_t *package_job_timer_ = nullptr;
    lv_timer_t *esc_hold_timer_ = nullptr;
    lv_timer_t *sync_timer_ = nullptr;
};

} // namespace appstore_ui
