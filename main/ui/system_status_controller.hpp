#pragma once

#include "system_status_state.hpp"

#include <cstdint>
#include <functional>

namespace appstore_ui {

class SystemStatusController
{
public:
    struct Dependencies {
        std::function<cp0_wifi_status_t()> read_wifi;
        std::function<cp0_battery_info_t()> read_battery;
        std::function<void()> shutdown;
        std::function<void(SystemStatusState &, uint32_t, bool)> update_overlay;
        std::function<void(SystemStatusState &, uint32_t, uint32_t)> animate_overlay;
    };

    SystemStatusController(SystemStatusState &state, Dependencies dependencies);

    void reset();
    bool refresh(bool force, uint32_t now, uint32_t refresh_interval_ms);
    void apply_battery(const cp0_battery_info_t &info, uint32_t now);
    bool tick_battery(uint32_t now, uint32_t flash_interval_ms);
    bool top_status_render_due(uint32_t now, bool allow_fast_render,
                               uint32_t charging_interval_ms,
                               uint32_t normal_interval_ms) const;

private:
    SystemStatusState &state_;
    Dependencies dependencies_;
};

} // namespace appstore_ui
