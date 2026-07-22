#include "status_message_state.hpp"

namespace appstore_ui {

bool StatusMessageState::visible(uint32_t now, uint32_t visible_ms)
{
    if (value_.empty()) {
        clear();
        return false;
    }
    if (tracked_value_ != value_) {
        tracked_value_ = value_;
        start_tick_ = now;
        visible_ms_ = visible_ms;
        return true;
    }
    if (start_tick_ != 0 && static_cast<uint32_t>(now - start_tick_) >= visible_ms_) {
        clear();
        return false;
    }
    return true;
}

void StatusMessageState::clear()
{
    value_.clear();
    tracked_value_.clear();
    start_tick_ = 0;
    visible_ms_ = 0;
}

} // namespace appstore_ui
