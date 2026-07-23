#pragma once

#include "appstore_client.hpp"
#include "lvgl/lvgl.h"

#include <string>

namespace appstore_ui {

class AppStoreShortcutBar
{
public:
    static void render_catalog(lv_obj_t *root, const appstore::StoreApp *app);
    static void render_detail(lv_obj_t *root, const std::string &app_dir,
                              const appstore::StoreApp &app);
    static void render_settings(lv_obj_t *root, bool has_entry, bool builtin_entry,
                                bool actions_available);
};

} // namespace appstore_ui
