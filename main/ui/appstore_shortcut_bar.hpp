#pragma once

#include "appstore_client.hpp"
#include "appstore_image_renderer.hpp"
#include "lvgl/lvgl.h"

#include <string>

namespace appstore_ui {

class AppStoreShortcutBar
{
public:
    static void render_catalog(lv_obj_t *root, AppStoreImageRenderer &images);
    static void render_detail(lv_obj_t *root, AppStoreImageRenderer &images,
                              const std::string &app_dir, const appstore::StoreApp &app);
};

} // namespace appstore_ui
