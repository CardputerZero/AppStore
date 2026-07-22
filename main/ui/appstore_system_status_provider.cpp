#include "appstore_system_status_provider.hpp"

#include "hal_lvgl_bsp.h"

namespace appstore_ui {

cp0_wifi_status_t AppStoreSystemStatusProvider::read_wifi()
{
    cp0_wifi_status_t status{};
    if (cp0_wifi_status_read(&status) != 0) status = {};
    return status;
}

cp0_battery_info_t AppStoreSystemStatusProvider::read_battery()
{
    return cp0_battery_read();
}

void AppStoreSystemStatusProvider::shutdown()
{
    cp0_system_shutdown();
}

} // namespace appstore_ui
