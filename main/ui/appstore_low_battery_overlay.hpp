#pragma once

#include "system_status_state.hpp"
#include "lvgl/lvgl.h"

#include <cstdint>

namespace appstore_ui {

class AppStoreLowBatteryOverlay
{
public:
    ~AppStoreLowBatteryOverlay();

    void update(SystemStatusState &status, uint32_t now, bool force = false);
    void animate(SystemStatusState &status, uint32_t now, uint32_t flash_interval_ms);
    void destroy();

private:
    void create();

    lv_obj_t *root_ = nullptr;
    lv_obj_t *tint_ = nullptr;
    lv_obj_t *countdown_ = nullptr;
};

} // namespace appstore_ui
