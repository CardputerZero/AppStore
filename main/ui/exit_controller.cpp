#include "exit_controller.hpp"

namespace appstore_ui {

void ExitController::reset()
{
    requested_ = 0;
    job_cancel_sent_ = false;
    background_cancel_sent_ = false;
    job_retry_tick_ = 0;
    background_retry_tick_ = 0;
    esc_press_tick_ = 0;
    esc_pressed_ = false;
    esc_long_consumed_ = false;
}

void ExitController::esc_pressed(uint32_t now)
{
    if (esc_pressed_) return;
    esc_pressed_ = true;
    esc_long_consumed_ = false;
    esc_press_tick_ = now;
}

bool ExitController::esc_released(uint32_t now, uint32_t hold_ms)
{
    const bool short_press = esc_pressed_ && !esc_long_consumed_ &&
        static_cast<uint32_t>(now - esc_press_tick_) < hold_ms;
    esc_pressed_ = false;
    esc_long_consumed_ = false;
    return short_press;
}

bool ExitController::consume_esc_hold(uint32_t now, uint32_t hold_ms)
{
    if (!esc_pressed_ || esc_long_consumed_ ||
        static_cast<uint32_t>(now - esc_press_tick_) < hold_ms) return false;
    esc_long_consumed_ = true;
    return true;
}

} // namespace appstore_ui
