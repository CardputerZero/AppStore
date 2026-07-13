#include "../main/ui/low_battery_flow.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    using appstore_ui::LowBatteryFlow;
    using appstore_ui::LowBatteryWarning;

    LowBatteryFlow flow;
    flow.update(true, 5, false, 100);
    assert(flow.warning() == LowBatteryWarning::None);
    flow.update(true, 4, false, 200);
    assert(flow.warning() == LowBatteryWarning::Low);
    assert(!flow.confirm_shutdown(true, false, 60000));

    flow.update(true, 0, false, 1000);
    assert(flow.warning() == LowBatteryWarning::ShutdownCountdown);
    assert(flow.seconds_until_shutdown(1000) == 15);
    assert(flow.seconds_until_shutdown(15001) == 1);
    assert(!flow.confirm_shutdown(true, false, 15999));
    assert(flow.shutdown_due(16000));
    assert(flow.confirm_shutdown(true, false, 16000));
    assert(!flow.shutdown_due(16000));
    assert(!flow.confirm_shutdown(true, false, 17000));

    flow.update(true, 0, true, 18000);
    assert(flow.warning() == LowBatteryWarning::None);
    assert(!flow.confirm_shutdown(true, false, 60000));

    flow.update(true, 0, false, 20000);
    flow.update(true, 0, true, 30000);
    assert(flow.warning() == LowBatteryWarning::None);
    assert(!flow.confirm_shutdown(true, false, 40000));

    flow.update(true, 0, false, 50000);
    assert(flow.shutdown_due(65000));
    flow.update(true, 0, true, 65000);
    assert(!flow.shutdown_due(65000));
    assert(!flow.confirm_shutdown(true, false, 65000));

    flow.update(true, 0, false, UINT32_MAX - 5000u);
    assert(!flow.confirm_shutdown(true, false, 4999));
    assert(flow.confirm_shutdown(true, false, 9999));

    LowBatteryFlow invalid_reading;
    invalid_reading.update(true, 0, false, 1000);
    assert(!invalid_reading.confirm_shutdown(false, false, 16000));
    assert(!invalid_reading.confirm_shutdown(false, false, 18999));
    assert(invalid_reading.confirm_shutdown(false, false, 19000));

    LowBatteryFlow recovered_charging;
    recovered_charging.update(true, 0, false, 1000);
    assert(!recovered_charging.confirm_shutdown(false, false, 16000));
    assert(!recovered_charging.confirm_shutdown(true, true, 17000));
    assert(recovered_charging.warning() == LowBatteryWarning::None);

    flow.update(true, 4, false, 12000);
    flow.update(false, 80, true, 13000);
    assert(flow.warning() == LowBatteryWarning::Low);
    flow.update(true, 80, false, 14000);
    assert(flow.warning() == LowBatteryWarning::None);

    flow.update(true, 0, false, 70000);
    flow.reset();
    assert(flow.warning() == LowBatteryWarning::None);
    assert(!flow.shutdown_due(90000));
}
