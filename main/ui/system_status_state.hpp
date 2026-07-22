#pragma once

#include "cp0_lvgl_app.h"
#include "low_battery_flow.hpp"

#include <cstdint>

namespace appstore_ui {

struct SystemStatusState {
    cp0_wifi_status_t wifi{};
    cp0_battery_info_t battery{};
    LowBatteryFlow low_battery;
    LowBatteryWarning rendered_warning = LowBatteryWarning::None;
    uint32_t low_battery_flash_tick = 0;
    uint32_t rendered_shutdown_seconds = 0;
    uint32_t refresh_tick = 0;
    uint32_t last_render_tick = 0;

    void reset()
    {
        wifi = {};
        battery = {};
        low_battery.reset();
        rendered_warning = LowBatteryWarning::None;
        low_battery_flash_tick = 0;
        rendered_shutdown_seconds = 0;
        refresh_tick = 0;
        last_render_tick = 0;
    }
};

} // namespace appstore_ui
