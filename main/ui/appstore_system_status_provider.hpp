#pragma once

#include "cp0_lvgl_app.h"

namespace appstore_ui {

class AppStoreSystemStatusProvider
{
public:
    static cp0_wifi_status_t read_wifi();
    static cp0_battery_info_t read_battery();
    static void shutdown();
};

} // namespace appstore_ui
