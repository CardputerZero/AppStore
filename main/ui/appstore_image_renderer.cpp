#include "appstore_image_renderer.hpp"

#include "appstore_paths.hpp"
#include "cp0_lvgl_app.h"

#include <algorithm>

namespace appstore_ui {

void AppStoreImageRenderer::begin_frame()
{
    sources_.clear();
    sources_.reserve(12);
}

const char *AppStoreImageRenderer::retain_source(std::string source)
{
    sources_.push_back(std::move(source));
    return sources_.back().c_str();
}

bool AppStoreImageRenderer::draw_packaged(lv_obj_t *root, const std::string &name, int x, int y)
{
#if LV_USE_LODEPNG && LV_USE_FS_POSIX
    const char *path = cp0_file_path_c(name.c_str());
    if (!path || !path[0]) return false;
    lv_obj_t *image = lv_image_create(root);
    lv_image_set_src(image, path);
    lv_obj_set_pos(image, x, y);
    return true;
#else
    (void)root; (void)name; (void)x; (void)y;
    return false;
#endif
}

bool AppStoreImageRenderer::draw_app_icon(lv_obj_t *root, const std::string &app_dir,
                                          const appstore::StoreApp &app)
{
#if LV_USE_LODEPNG && LV_USE_FS_POSIX
    const std::string path = icon_file_path(app_dir, app);
    if (path.empty()) return false;
    constexpr int x = 10, y = 49, width = 68, height = 68;
    lv_obj_t *clip = lv_obj_create(root);
    lv_obj_remove_style_all(clip);
    lv_obj_set_pos(clip, x, y);
    lv_obj_set_size(clip, width, height);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clip, 0, 0);
    lv_obj_set_style_radius(clip, 10, 0);
    lv_obj_set_style_clip_corner(clip, true, 0);
    lv_obj_set_style_pad_all(clip, 0, 0);
    lv_obj_t *icon = lv_image_create(clip);
    lv_image_set_src(icon, retain_source(lvgl_posix_src(path)));
    lv_obj_update_layout(icon);
    int image_width = lv_obj_get_width(icon);
    int image_height = lv_obj_get_height(icon);
    if (image_width <= 0) image_width = 256;
    if (image_height <= 0) image_height = 256;
    int scale = std::min(width * 256 / image_width, height * 256 / image_height);
    if (scale <= 0) scale = 1;
    lv_image_set_scale(icon, static_cast<uint16_t>(scale));
    lv_obj_set_pos(icon, (width - image_width) / 2, (height - image_height) / 2);
    return true;
#else
    (void)root; (void)app_dir; (void)app;
    return false;
#endif
}

bool AppStoreImageRenderer::draw_screenshot(lv_obj_t *root, const std::string &path)
{
#if LV_USE_LODEPNG && LV_USE_FS_POSIX
    if (path.empty()) return false;
    constexpr int width = 320, height = 170;
    lv_obj_t *clip = lv_obj_create(root);
    lv_obj_remove_style_all(clip);
    lv_obj_set_pos(clip, 0, 0);
    lv_obj_set_size(clip, width, height);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clip, 0, 0);
    lv_obj_set_style_pad_all(clip, 0, 0);
    lv_obj_t *image = lv_image_create(clip);
    lv_image_set_src(image, retain_source(lvgl_posix_src(path)));
    lv_obj_update_layout(image);
    int image_width = lv_obj_get_width(image);
    int image_height = lv_obj_get_height(image);
    if (image_width <= 0) image_width = width;
    if (image_height <= 0) image_height = height;
    int scale = std::max(width * 256 / image_width, height * 256 / image_height);
    if (scale <= 0) scale = 256;
    lv_image_set_scale(image, static_cast<uint16_t>(scale));
    lv_obj_update_layout(image);
    lv_obj_set_pos(image, (width - image_width * scale / 256) / 2,
                   (height - image_height * scale / 256) / 2);
    return true;
#else
    (void)root; (void)path;
    return false;
#endif
}

} // namespace appstore_ui
