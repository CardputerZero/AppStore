#include "system_status_state.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::SystemStatusState state;
    state.wifi.connected = 1;
    state.battery.valid = 1;
    state.battery.soc = 50;
    state.rendered_warning = appstore_ui::LowBatteryWarning::Low;
    state.refresh_tick = 100;
    state.reset();
    assert(!state.wifi.connected);
    assert(!state.battery.valid);
    assert(state.rendered_warning == appstore_ui::LowBatteryWarning::None);
    assert(state.refresh_tick == 0);
    std::cout << "system status state tests passed\n";
}
