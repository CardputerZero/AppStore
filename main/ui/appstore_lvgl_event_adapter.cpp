#include "appstore_lvgl_event_adapter.hpp"

#include "keyboard_input.h"

#include <cctype>
#include <utility>

namespace appstore_ui {

AppStoreLvglEventAdapter::AppStoreLvglEventAdapter(KeyHandler key_handler,
                                                   BatteryHandler battery_handler)
    : key_handler_(std::move(key_handler)), battery_handler_(std::move(battery_handler))
{
}

void AppStoreLvglEventAdapter::bind(lv_obj_t *root)
{
    lv_obj_add_event_cb(root, keyboard_callback,
                        static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), this);
    lv_obj_add_event_cb(root, battery_callback,
                        static_cast<lv_event_code_t>(lv_c_event[CP0_C_EVENT_BATTERY]), this);
}

char AppStoreLvglEventAdapter::lower_printable(const char *utf8)
{
    if (!utf8 || !utf8[0] || utf8[1]) return 0;
    const unsigned char ch = static_cast<unsigned char>(utf8[0]);
    if (!std::isprint(ch)) return 0;
    return static_cast<char>(std::tolower(ch));
}

void AppStoreLvglEventAdapter::keyboard_callback(lv_event_t *event)
{
    auto *self = static_cast<AppStoreLvglEventAdapter *>(lv_event_get_user_data(event));
    auto *item = static_cast<key_item *>(lv_event_get_param(event));
    if (!self || !item || !self->key_handler_) return;
    AppStoreKeyEvent key;
    key.code = item->key_code;
    key.mods = item->mods;
    key.ch = lower_printable(item->utf8);
    key.release = item->key_state == KBD_KEY_RELEASED;
    key.repeated = item->key_state == KBD_KEY_REPEATED;
    self->key_handler_(key);
}

void AppStoreLvglEventAdapter::battery_callback(lv_event_t *event)
{
    auto *self = static_cast<AppStoreLvglEventAdapter *>(lv_event_get_user_data(event));
    auto *info = static_cast<cp0_battery_info_t *>(lv_event_get_param(event));
    if (self && info && self->battery_handler_) self->battery_handler_(*info);
}

} // namespace appstore_ui
