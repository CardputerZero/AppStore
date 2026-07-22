#pragma once

#include <atomic>
#include <cstdint>
#include <csignal>

namespace appstore_ui {

class ExitController
{
public:
    void reset();
    void request() { requested_ = 1; }
    bool requested() const { return requested_ != 0; }

    void worker_started() { worker_count_.fetch_add(1); }
    void worker_finished() { worker_count_.fetch_sub(1); }
    int worker_count() const { return worker_count_.load(); }

    bool &job_cancel_sent() { return job_cancel_sent_; }
    bool &background_cancel_sent() { return background_cancel_sent_; }
    uint32_t &job_retry_tick() { return job_retry_tick_; }
    uint32_t &background_retry_tick() { return background_retry_tick_; }

    void esc_pressed(uint32_t now);
    bool esc_released(uint32_t now, uint32_t hold_ms);
    bool consume_esc_hold(uint32_t now, uint32_t hold_ms);

private:
    volatile sig_atomic_t requested_ = 0;
    bool job_cancel_sent_ = false;
    bool background_cancel_sent_ = false;
    uint32_t job_retry_tick_ = 0;
    uint32_t background_retry_tick_ = 0;
    std::atomic<int> worker_count_{0};
    uint32_t esc_press_tick_ = 0;
    bool esc_pressed_ = false;
    bool esc_long_consumed_ = false;
};

} // namespace appstore_ui
