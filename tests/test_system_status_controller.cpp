#include "system_status_controller.hpp"

#include <cassert>
#include <iostream>

int main()
{
    using namespace appstore_ui;
    SystemStatusState state;
    cp0_battery_info_t battery{};
    battery.valid = 1;
    battery.soc = 80;
    int wifi_reads = 0;
    int battery_reads = 0;
    int shutdowns = 0;
    int overlay_updates = 0;
    int overlay_animations = 0;
    SystemStatusController controller(
        state,
        {[&]() {
             ++wifi_reads;
             cp0_wifi_status_t wifi{};
             wifi.connected = 1;
             wifi.signal = -45;
             return wifi;
         },
         [&]() { ++battery_reads; return battery; },
         [&]() { ++shutdowns; },
         [&](SystemStatusState &, uint32_t, bool force) {
             assert(force);
             ++overlay_updates;
         },
         [&](SystemStatusState &, uint32_t, uint32_t interval) {
             assert(interval == 500);
             ++overlay_animations;
         }});

    controller.reset();
    assert(controller.refresh(true, 100, 5000));
    assert(wifi_reads == 1 && battery_reads == 1 && overlay_updates == 1);
    assert(state.wifi.connected && state.battery.soc == 80);
    assert(!controller.refresh(false, 200, 5000));
    assert(controller.refresh(false, 5100, 5000));
    assert(wifi_reads == 2 && battery_reads == 2);

    state.last_render_tick = 1000;
    battery.flags = 1;
    controller.apply_battery(battery, 1000);
    assert(!controller.top_status_render_due(1119, true, 120, 5000));
    assert(controller.top_status_render_due(1120, true, 120, 5000));
    assert(!controller.top_status_render_due(6000, false, 120, 5000));
    assert(controller.top_status_render_due(10100, false, 120, 5000));

    battery.flags = 0;
    battery.soc = 0;
    controller.apply_battery(battery, 20000);
    assert(!controller.tick_battery(34999, 500));
    assert(overlay_animations == 1);
    battery.flags = 1;
    assert(!controller.tick_battery(35000, 500));
    assert(shutdowns == 0);
    assert(state.low_battery.warning() == LowBatteryWarning::None);

    battery.flags = 0;
    controller.apply_battery(battery, 40000);
    assert(controller.tick_battery(55000, 500));
    assert(shutdowns == 1);

    std::cout << "system status controller tests passed\n";
}
