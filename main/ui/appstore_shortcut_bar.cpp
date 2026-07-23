#include "appstore_shortcut_bar.hpp"

namespace appstore_ui {
namespace {

void centered_strong_label(lv_obj_t *root, const std::string &text, int x, int y,
                           int w, int h, uint32_t color)
{
    for (int offset = 0; offset < 2; ++offset) {
        lv_obj_t *label = lv_label_create(root);
        lv_obj_set_pos(label, x + offset, y);
        lv_obj_set_size(label, w, h);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_obj_set_style_text_letter_space(label, 0, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, text.c_str());
    }
}

void shortcut_label(lv_obj_t *root, int center_x,
                    const std::string &text, uint32_t color, int width = 56)
{
    centered_strong_label(root, text, center_x - width / 2, 151, width, 18, color);
}

} // namespace

void AppStoreShortcutBar::render_catalog(lv_obj_t *root,
                                         const appstore::StoreApp *app)
{
    shortcut_label(root, 48, "settings", 0xF0B429);
    shortcut_label(root, 104, "share", 0x168CE5);
    shortcut_label(root, 160, "search", 0x7ED957);
    if (app && appstore::can_install_app(*app))
        shortcut_label(root, 216, app->installed ? "reinstall" : "install", 0xB069FF);
    if (app)
        shortcut_label(root, 272, "detail", 0xFF8A2A);
}

void AppStoreShortcutBar::render_detail(lv_obj_t *root, const std::string &app_dir,
                                        const appstore::StoreApp &app)
{
    (void)app_dir;
    shortcut_label(root, 48, "back", 0xF0B429);
    shortcut_label(root, 104, "shots", 0x58A6FF);
    if (!app.installed && appstore::can_install_app(app))
        shortcut_label(root, 160, "install", 0xB069FF);
    if (appstore::can_reinstall_app(app))
        shortcut_label(root, 160, "reinstall", 0xB069FF);
    if (appstore::can_upgrade_app(app))
        shortcut_label(root, 216, "upgrade", 0xB069FF);
    if (app.installed)
        shortcut_label(root, 272, "remove", 0xFF5B4A);
}

void AppStoreShortcutBar::render_settings(lv_obj_t *root, bool has_entry,
                                          bool builtin_entry,
                                          bool actions_available)
{
    if (!actions_available) return;
    if (has_entry && !builtin_entry)
        shortcut_label(root, 104, "edit", 0x168CE5);
    if (has_entry && !builtin_entry)
        shortcut_label(root, 216, "delete", 0xFF5B4A);
    shortcut_label(root, 272, "sync", 0x58A6FF);
}

} // namespace appstore_ui
