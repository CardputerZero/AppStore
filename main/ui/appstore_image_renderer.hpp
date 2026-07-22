#pragma once

#include "appstore_client.hpp"
#include "lvgl/lvgl.h"

#include <string>
#include <vector>

namespace appstore_ui {

class AppStoreImageRenderer
{
public:
    void begin_frame();
    bool draw_packaged(lv_obj_t *root, const std::string &name, int x, int y);
    bool draw_app_icon(lv_obj_t *root, const std::string &app_dir,
                       const appstore::StoreApp &app);
    bool draw_screenshot(lv_obj_t *root, const std::string &path);

private:
    const char *retain_source(std::string source);
    std::vector<std::string> sources_;
};

} // namespace appstore_ui
