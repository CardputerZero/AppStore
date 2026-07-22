#pragma once

#include "appstore_input_controller.hpp"
#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "lvgl/lvgl.h"

#include <functional>

namespace appstore_ui {

class AppStoreLvglEventAdapter
{
public:
    using KeyHandler = std::function<void(const AppStoreKeyEvent &)>;
    using BatteryHandler = std::function<void(const cp0_battery_info_t &)>;

    AppStoreLvglEventAdapter(KeyHandler key_handler, BatteryHandler battery_handler);

    void bind(lv_obj_t *root);

private:
    static char lower_printable(const char *utf8);
    static void keyboard_callback(lv_event_t *event);
    static void battery_callback(lv_event_t *event);

    KeyHandler key_handler_;
    BatteryHandler battery_handler_;
};

} // namespace appstore_ui
