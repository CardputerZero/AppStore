#include "system_status_controller.hpp"

#include <utility>

namespace appstore_ui {

SystemStatusController::SystemStatusController(SystemStatusState &state,
                                               Dependencies dependencies)
    : state_(state), dependencies_(std::move(dependencies))
{
}

void SystemStatusController::reset()
{
    state_.reset();
}

void SystemStatusController::apply_battery(const cp0_battery_info_t &info, uint32_t now)
{
    state_.battery = info;
    state_.low_battery.update(info.valid != 0, info.soc, (info.flags & 1) != 0, now);
    if (dependencies_.update_overlay) dependencies_.update_overlay(state_, now, true);
}

bool SystemStatusController::refresh(bool force, uint32_t now,
                                     uint32_t refresh_interval_ms)
{
    if (!force && state_.refresh_tick != 0 &&
        static_cast<uint32_t>(now - state_.refresh_tick) < refresh_interval_ms)
        return false;
    if (dependencies_.read_wifi) state_.wifi = dependencies_.read_wifi();
    if (dependencies_.read_battery) apply_battery(dependencies_.read_battery(), now);
    state_.refresh_tick = now;
    return true;
}

bool SystemStatusController::tick_battery(uint32_t now, uint32_t flash_interval_ms)
{
    if (dependencies_.animate_overlay)
        dependencies_.animate_overlay(state_, now, flash_interval_ms);
    if (!state_.low_battery.shutdown_due(now)) return false;
    const cp0_battery_info_t confirmation = dependencies_.read_battery
        ? dependencies_.read_battery() : cp0_battery_info_t{};
    apply_battery(confirmation, now);
    if (!state_.low_battery.confirm_shutdown(
            confirmation.valid != 0, (confirmation.flags & 1) != 0, now))
        return false;
    if (dependencies_.shutdown) dependencies_.shutdown();
    return true;
}

bool SystemStatusController::top_status_render_due(
    uint32_t now, bool allow_fast_render, uint32_t charging_interval_ms,
    uint32_t normal_interval_ms) const
{
    const bool charging = state_.battery.valid && (state_.battery.flags & 1);
    const bool fast = charging && allow_fast_render;
    return (fast && static_cast<uint32_t>(now - state_.last_render_tick) >=
                        charging_interval_ms) ||
           (!fast && state_.refresh_tick != 0 &&
            static_cast<uint32_t>(now - state_.refresh_tick) >= normal_interval_ms);
}

} // namespace appstore_ui
