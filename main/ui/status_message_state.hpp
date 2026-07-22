#pragma once

#include <cstdint>
#include <string>

namespace appstore_ui {

class StatusMessageState
{
public:
    std::string &value() { return value_; }
    const std::string &value() const { return value_; }

    bool visible(uint32_t now, uint32_t visible_ms = 6000);
    void clear();

private:
    std::string value_;
    std::string tracked_value_;
    uint32_t start_tick_ = 0;
    uint32_t visible_ms_ = 0;
};

} // namespace appstore_ui
