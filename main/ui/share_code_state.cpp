#include "share_code_state.hpp"

namespace appstore_ui {

void ShareCodeState::open(uint32_t now)
{
    input_.clear();
    message_ = "Enter a code from CardputerZero Hub.";
    opened_tick_ = now;
}

bool ShareCodeState::append(char ch, uint32_t now, size_t max_length)
{
    if (ch < 32 || ch > 126 || input_.size() >= max_length) return false;
    if (input_.empty() && ch == 'c' && static_cast<uint32_t>(now - opened_tick_) < 250)
        return false;
    input_.push_back(ch);
    message_ = "Enter opens the app detail page.";
    return true;
}

bool ShareCodeState::erase_last()
{
    if (input_.empty()) return false;
    input_.pop_back();
    return true;
}

} // namespace appstore_ui
