#include "appstore_low_battery_overlay.hpp"

#include "appstore_fonts.hpp"

#include <string>

namespace appstore_ui {
namespace {

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                const lv_font_t *font, uint32_t color, bool centered)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_text_font(object, font_for_text(text, font), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(object, 0, 0);
    lv_obj_set_style_text_align(object, centered ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_label_set_text(object, text);
    return object;
}

} // namespace

AppStoreLowBatteryOverlay::~AppStoreLowBatteryOverlay()
{
    destroy();
}

void AppStoreLowBatteryOverlay::create()
{
    if (root_) return;
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, 320, 170);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_IGNORE_LAYOUT);

    tint_ = lv_obj_create(root_);
    lv_obj_remove_style_all(tint_);
    lv_obj_set_size(tint_, 320, 170);
    lv_obj_set_style_bg_color(tint_, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(tint_, LV_OPA_20, 0);
    lv_obj_clear_flag(tint_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(root_);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 18, 39);
    lv_obj_set_size(panel, 284, 94);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x160000), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_80, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFF3030), 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    label(panel, "LOW BATTERY", 12, 10, 260, 18, &lv_font_montserrat_14, 0xFFFFFF, true);
    label(panel, "LOW BATTERY", 13, 10, 260, 18, &lv_font_montserrat_14, 0xFFFFFF, true);
    countdown_ = label(panel, "", 12, 34, 260, 18, &lv_font_montserrat_14, 0xFF4444, true);
    label(panel, "Shut down now or connect a charger.", 12, 62, 260, 15,
          &lv_font_montserrat_10, 0xFFFFFF, true);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void AppStoreLowBatteryOverlay::update(SystemStatusState &status, uint32_t now, bool force)
{
    const LowBatteryWarning warning = status.low_battery.warning();
    if (warning == LowBatteryWarning::None) {
        if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        status.rendered_warning = warning;
        status.rendered_shutdown_seconds = 0;
        return;
    }

    create();
    lv_obj_move_foreground(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    const bool warning_changed = warning != status.rendered_warning;
    if (warning_changed) {
        status.low_battery_flash_tick = now;
        lv_obj_set_style_bg_opa(tint_, LV_OPA_20, 0);
    }
    const uint32_t seconds = status.low_battery.seconds_until_shutdown(now);
    if (force || warning_changed || seconds != status.rendered_shutdown_seconds) {
        const std::string text = warning == LowBatteryWarning::ShutdownCountdown
            ? "Power off in " + std::to_string(seconds) + "s" : "Battery below 5%";
        lv_label_set_text(countdown_, text.c_str());
        status.rendered_warning = warning;
        status.rendered_shutdown_seconds = seconds;
    }
}

void AppStoreLowBatteryOverlay::animate(SystemStatusState &status, uint32_t now,
                                        uint32_t flash_interval_ms)
{
    update(status, now);
    if (!root_ || status.low_battery.warning() == LowBatteryWarning::None ||
        lv_tick_elaps(status.low_battery_flash_tick) < flash_interval_ms) return;
    status.low_battery_flash_tick = now;
    const lv_opa_t opacity = lv_obj_get_style_bg_opa(tint_, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tint_, opacity == LV_OPA_20 ? LV_OPA_50 : LV_OPA_20, 0);
}

void AppStoreLowBatteryOverlay::destroy()
{
    if (root_) lv_obj_del(root_);
    root_ = nullptr;
    tint_ = nullptr;
    countdown_ = nullptr;
}

} // namespace appstore_ui
