#include "appstore_shortcut_bar.hpp"

#include "appstore_paths.hpp"

namespace appstore_ui {
namespace {

void centered_strong_label(lv_obj_t *root, const std::string &text, int x, int y,
                           int w, int h, uint32_t color)
{
    for (int offset = 0; offset < 2; ++offset) {
        lv_obj_t *label = lv_label_create(root);
        lv_obj_set_pos(label, x + offset, y);
        lv_obj_set_size(label, w, h);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_obj_set_style_text_letter_space(label, 0, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, text.c_str());
    }
}

void button(lv_obj_t *root, AppStoreImageRenderer &images, int spec_center_x,
            const std::string &icon, const std::string &text, uint32_t color)
{
    const int center_x = spec_center_x - 132;
    const int icon_x = center_x - 8;
    const int icon_y = 140;
    if (!images.draw_packaged(root, icon, icon_x, icon_y))
        centered_strong_label(root, "*", center_x - 8, icon_y + 1, 16, 10, color);
    centered_strong_label(root, text, center_x - 27, 158, 54, 10, color);
}

} // namespace

void AppStoreShortcutBar::render_catalog(lv_obj_t *root, AppStoreImageRenderer &images)
{
    button(root, images, 180, "appstore-shortcut-settings.png", "settings", 0xF0B429);
    button(root, images, 236, "appstore-shortcut-sharecode.png", "share", 0x168CE5);
    button(root, images, 292, "appstore-shortcut-search.png", "search", 0x7ED957);
    button(root, images, 348, "appstore-shortcut-install.png", "install", 0xB069FF);
    button(root, images, 404, "appstore-shortcut-detail.png", "detail", 0xFF8A2A);
}

void AppStoreShortcutBar::render_detail(lv_obj_t *root, AppStoreImageRenderer &images,
                                        const std::string &app_dir, const appstore::StoreApp &app)
{
    button(root, images, 180, "appstore-shortcut-back.png", "back", 0xF0B429);
    if (!detail_screenshot_paths(app_dir, app).empty())
        button(root, images, 236, "appstore-shortcut-screenshots.png", "shots", 0x58A6FF);
    if (!app.installed && appstore::can_install_app(app))
        button(root, images, 292, "appstore-shortcut-install.png", "install", 0xB069FF);
    if (appstore::can_reinstall_app(app))
        button(root, images, 292, "appstore-shortcut-install.png", "reinstall", 0xB069FF);
    if (appstore::can_upgrade_app(app))
        button(root, images, 348, "appstore-shortcut-upgrade.png", "upgrade", 0xB069FF);
    if (app.installed)
        button(root, images, 404, "appstore-shortcut-delete.png", "remove", 0xFF5B4A);
}

} // namespace appstore_ui
