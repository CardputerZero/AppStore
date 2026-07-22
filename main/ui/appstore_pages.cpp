#include "appstore_pages.hpp"

#include "appstore_fonts.hpp"

#include <algorithm>

namespace appstore_ui {
namespace {

lv_obj_t *label(lv_obj_t *root, const std::string &text, int x, int y, int w, int h,
                const lv_font_t *font, uint32_t color,
                lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    lv_obj_t *obj = lv_label_create(root);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_label_set_long_mode(obj, mode);
    lv_label_set_text(obj, text.c_str());
    return obj;
}

lv_obj_t *center_label(lv_obj_t *root, const std::string &text, int x, int y, int w, int h,
                       const lv_font_t *font, uint32_t color,
                       lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    lv_obj_t *obj = label(root, text, x, y, w, h, font, color, mode);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

void strong_label(lv_obj_t *root, const std::string &text, int x, int y, int w, int h,
                  const lv_font_t *font, uint32_t color,
                  lv_label_long_mode_t mode = LV_LABEL_LONG_DOT)
{
    label(root, text, x, y, w, h, font, color, mode);
    label(root, text, x + 1, y, w, h, font, color, mode);
}

void center_strong_label(lv_obj_t *root, const std::string &text, int x, int y, int w, int h,
                         const lv_font_t *font, uint32_t color,
                         lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    center_label(root, text, x, y, w, h, font, color, mode);
    center_label(root, text, x + 1, y, w, h, font, color, mode);
}

lv_obj_t *box(lv_obj_t *root, int x, int y, int w, int h, uint32_t color,
              uint32_t border = 0x2A3A46, int border_width = 1, int radius = 0,
              lv_opa_t bg_opa = LV_OPA_COVER)
{
    lv_obj_t *obj = lv_obj_create(root);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, bg_opa, 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    return obj;
}

void set_progress_scan_x(void *object, int32_t x)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(object), x);
}

void modal_backdrop(lv_obj_t *root)
{
    lv_obj_t *obj = lv_obj_create(root);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 320, 170);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(obj, static_cast<lv_opa_t>(176), 0);
    lv_obj_set_style_blur_backdrop(obj, true, 0);
    lv_obj_set_style_blur_radius(obj, 6, 0);
    lv_obj_set_style_blur_quality(obj, LV_BLUR_QUALITY_SPEED, 0);
}

void scrolling_status_label(lv_obj_t *root, const std::string &text, int x, int y, int w, int h)
{
    lv_obj_t *obj = label(root, text, x, y, w, h, &lv_font_montserrat_10, 0xCCCC33,
                          LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(obj, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void radio_option(lv_obj_t *root, int x, int y, const std::string &text,
                  bool selected, bool focused)
{
    const uint32_t border = focused ? 0xCCCC33 : (selected ? 0x58A6FF : 0x6E7681);
    const uint32_t text_color = selected ? 0xFFFFFF : 0x8B949E;
    box(root, x, y + 2, 10, 10, 0x0D1117, border, 1, 5);
    if (selected)
        box(root, x + 3, y + 5, 4, 4, focused ? 0xCCCC33 : 0x58A6FF,
            focused ? 0xCCCC33 : 0x58A6FF, 0, 2);
    label(root, text, x + 15, y, 70, 12, &lv_font_montserrat_10, text_color,
          LV_LABEL_LONG_DOT);
}

} // namespace

AppStoreUiPage::AppStoreUiPage()
{
    set_page_title("AppStore");
    enable_top_bar();
}

void AppStoreUiPage::activate()
{
    cp0_lvgl_start_app_page(*this);
}

void InitializationProgressPage::render(const PageRenderContext &context,
                                        const InitializationProgressViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x080B10, 0x080B10, 0);
    box(root, 0, 20, 320, 22, 0x101B2D, 0x101B2D, 0);
    const bool preparing_catalog = model.phase == "CATALOG";
    label(root, preparing_catalog ? "Preparing App Catalog" : "Checking Network",
          8, 24, 190, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(root, "Hold ESC", 254, 24, 60, 14, &lv_font_montserrat_10, 0xAECBFA);
    center_strong_label(root, appstore::upper_ascii(model.phase), 28, 47, 264, 16,
                        &lv_font_montserrat_12, 0x58A6FF, LV_LABEL_LONG_DOT);
    label(root, appstore::one_line(model.url, 62), 18, 68, 284, 20,
          &lv_font_montserrat_10, 0xC9D1D9, LV_LABEL_LONG_DOT);
    box(root, 20, 94, 280, 15, 0x111923, 0x30363D, 1, 3);
    for (int i = 0; i < 7; ++i) box(root, 24 + i * 39, 98, 28, 7, 0x162235, 0x162235, 0, 2);
    if (model.percent >= 0) {
        const int fill = std::max(5, std::min(272, model.percent * 272 / 100));
        box(root, 24, 98, fill, 7, 0x2FEC8D, 0x2FEC8D, 0, 2);
        box(root, std::max(24, std::min(286, 24 + fill - 4)), 96, 8, 11,
            0xD8FF6A, 0xD8FF6A, 0, 3);
    } else {
        lv_obj_t *scan = box(root, 24, 97, 52, 9, 0x2FEC8D, 0xD8FF6A, 1, 3);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, scan);
        lv_anim_set_values(&animation, 24, 244);
        lv_anim_set_duration(&animation, 900);
        lv_anim_set_playback_duration(&animation, 900);
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&animation, set_progress_scan_x);
        lv_anim_start(&animation);
    }
    center_label(root, appstore::one_line(model.detail, 52), 18, 119, 284, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    center_strong_label(root, model.cancel_requested ? "Cancelling..." :
                        (preparing_catalog ? "Building app list...   Hold ESC: Exit" :
                         "Checking connection...   Hold ESC: Exit"), 18, 151, 284, 12,
                        &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    if (!model.failed) return;

    modal_backdrop(root);
    box(root, 16, 27, 288, 136, 0x111923, 0xF85149, 2, 4);
    box(root, 16, 27, 288, 24, 0x8B2F34, 0x8B2F34, 0);
    center_strong_label(root, model.error_title, 32, 32, 256, 15,
                        &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
    center_strong_label(root, appstore::one_line(model.error_message, 38), 30, 61, 260, 15,
                        &lv_font_montserrat_12, 0xFFD2D2, LV_LABEL_LONG_DOT);
    int y = 82;
    for (const auto &line_text : model.error_detail_lines) {
        center_label(root, appstore::one_line(line_text, 44), 30, y, 260, 12,
                     &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
        y += 13;
        if (y > 95) break;
    }
    const bool exit_focused = model.failure_focus == 0;
    const bool settings_focused = model.failure_focus == 1;
    box(root, 45, 119, 92, 19, exit_focused ? 0x8B2F34 : 0x4B1F24,
        exit_focused ? 0xCCCC33 : 0xF85149, exit_focused ? 2 : 1, 2);
    center_strong_label(root, "EXIT", 49, 123, 84, 12,
                        &lv_font_montserrat_10, 0xFFFFFF);
    box(root, 166, 119, 109, 19, settings_focused ? 0x2EA043 : 0x1A6A2A,
        settings_focused ? 0xCCCC33 : 0x2EA043, settings_focused ? 2 : 1, 2);
    center_strong_label(root, "SETTINGS", 170, 123, 101, 12,
                        &lv_font_montserrat_10, 0xFFFFFF);
    center_label(root, "Left/Right select   Enter confirm", 48, 146, 224, 12,
                 &lv_font_montserrat_10, 0xCCCC33);
}

void CatalogDisplayPage::render(const PageRenderContext &context,
                                const CatalogDisplayViewModel &model,
                                const std::function<void()> &draw_category,
                                const std::function<void()> &draw_icon,
                                const std::function<void()> &draw_shortcuts)
{
    context.prepare();
    context.draw_system_bar();
    draw_category();
    draw_icon();
    lv_obj_t *root = screen();
    if (model.show_empty) {
        strong_label(root, "NO APPS", 106, 72, 130, 24, &lv_font_montserrat_20, 0xFFFFFF);
    } else {
        const int y_pos[] = {39, 55, 70, 104, 119};
        const lv_font_t *fonts[] = {&lv_font_montserrat_10, &lv_font_montserrat_12,
            &lv_font_montserrat_20, &lv_font_montserrat_12, &lv_font_montserrat_10};
        const uint32_t colors[] = {0x3D3D3D, 0x575757, 0xFFFFFF, 0x575757, 0x3D3D3D};
        for (const auto &row : model.rows) {
            const std::string text = row.selected ? appstore::upper_ascii(row.name) :
                appstore::one_line(appstore::upper_ascii(row.name), 16);
            const lv_font_t *font = context.font_for_text(text, fonts[row.row]);
            strong_label(root, text, 106, y_pos[row.row], row.selected ? 144 : 130,
                         row.selected ? 24 : 16, font, colors[row.row],
                         row.selected ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
            if (row.selected) {
                strong_label(root, "V" + appstore::one_line(row.version.empty() ? "0" : row.version, 6),
                             264, y_pos[row.row] + 3, 52, 16, &lv_font_montserrat_14,
                             0x8B8B8B, LV_LABEL_LONG_DOT);
                strong_label(root, (row.author.empty() ? "unknown" : row.author) + ".",
                             109, y_pos[row.row] + 22, 190, 12,
                             &lv_font_montserrat_10, 0xBDBDBD);
            }
        }
        strong_label(root, std::to_string(model.selected_index + 1) + "/" +
                     std::to_string(model.app_count), 284, 119, 34, 15,
                     &lv_font_montserrat_14, 0xFFFFFF, LV_LABEL_LONG_DOT);
    }
    if (model.show_status) scrolling_status_label(root, model.status, 106, 128, 172, 10);
    draw_shortcuts();
}

void CatalogDisplayPage::render_text_entry(const PageRenderContext &context,
                                           const TextEntryViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(root, 0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(root, model.title, 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);
    center_strong_label(root, model.prompt, 68, 51, 184, 16,
                        &lv_font_montserrat_12, model.accent, LV_LABEL_LONG_DOT);
    box(root, model.input_x, 69, model.input_width, 45, 0x111923, model.accent, 2, 3);
    const std::string display = model.input.empty() ? model.placeholder :
        appstore::upper_ascii(model.input);
    center_strong_label(root, appstore::one_line(display, model.display_limit),
                        model.input_x + 10, 81, model.input_width - 20, 23,
                        &lv_font_montserrat_20,
                        model.input.empty() ? 0x6E7681 : 0xE6EDF3, LV_LABEL_LONG_DOT);
    center_label(root, appstore::one_line(model.message, 48), 10, 122, 300, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    label(root, "Enter Open   Backspace Delete   Esc Back", 10, 153, 300, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void CatalogDisplayPage::render_search(const PageRenderContext &context,
                                       const SearchPageViewModel &model)
{
    if (!model.results_active) {
        render_text_entry(context, {"Search", "SEARCH APPS", "app name", model.input,
                                    model.message, 0x7ED957, 50, 220, 16});
        return;
    }
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(root, 0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(root, "Search", 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);
    label(root, "QUERY", 10, 48, 42, 12, &lv_font_montserrat_10, 0x7ED957);
    box(root, 54, 44, 116, 20, 0x111923, 0x2EA043, 1, 2);
    label(root, appstore::one_line(appstore::upper_ascii(model.input), 12), 60, 49, 104, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(root, std::to_string(model.result_count) + " RESULTS", 182, 48, 128, 12,
          &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    for (size_t row = 0; row < model.rows.size(); ++row) {
        const auto &item = model.rows[row];
        const int y = 68 + static_cast<int>(row) * 18;
        if (item.selected) box(root, 10, y - 2, 300, 17, 0x142817, 0x7ED957, 1, 2);
        const std::string name = appstore::one_line(appstore::upper_ascii(item.name), 18);
        strong_label(root, name, 16, y, 138, 13,
                     context.font_for_text(name, item.selected ? &lv_font_montserrat_12 :
                                           &lv_font_montserrat_10),
                     item.selected ? 0xFFFFFF : 0xB8B8B8, LV_LABEL_LONG_DOT);
        label(root, appstore::one_line(item.meta, 14), 164, y + 1, 88, 12,
              &lv_font_montserrat_10, item.selected ? 0x7ED957 : 0x8B949E,
              LV_LABEL_LONG_DOT);
        label(root, "V" + appstore::one_line(item.version.empty() ? "0" : item.version, 5),
              260, y + 1, 48, 12, &lv_font_montserrat_10, 0x8B8B8B, LV_LABEL_LONG_DOT);
    }
    label(root, "Up/Down Select   Enter Open   Backspace Edit", 10, 153, 300, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void AppDetailPage::render(const PageRenderContext &context, const AppDetailViewModel &model,
                           const std::function<void(const appstore::StoreApp &)> &draw_shortcuts)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    if (!model.has_app) {
        label(root, "No selected app", 10, 50, 304, 14, &lv_font_montserrat_12, 0xE6EDF3);
        return;
    }
    box(root, 0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(root, model.title, 8, 24, 220, 15,
          context.font_for_text(model.title, &lv_font_montserrat_12), 0xFFFFFF, LV_LABEL_LONG_DOT);
    label(root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);
    label(root, "State :", 10, 47, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(root, model.state, 62, 47, 105, 12, &lv_font_montserrat_10,
          model.app.installed ? 0xCCCC33 : 0xE6EDF3);
    label(root, "Size :", 176, 47, 42, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(root, model.app.size, 220, 47, 84, 12, &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    const struct { const char *name; std::string value; int y; } fields[] = {
        {"Review:", model.review, 62}, {"Author:", model.app.author.empty() ? "-" : model.app.author, 77},
        {"Git   :", model.app.git_url.empty() ? "-" : model.app.git_url, 92},
        {"Deps  :", model.app.dependencies.empty() ? "-" : model.app.dependencies, 107}};
    for (const auto &field : fields) {
        label(root, field.name, 10, field.y, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
        label(root, appstore::one_line(field.value, field.y == 62 ? 36 : 42), 62, field.y, 246, 12,
              &lv_font_montserrat_10, field.y == 62 ? (model.installable ? 0x52D05D : 0xF0C45C) : 0xE6EDF3,
              LV_LABEL_LONG_DOT);
    }
    if (model.show_status) {
        box(root, 8, 119, 304, 13, 0x0D1117, 0x0D1117, 0);
        scrolling_status_label(root, model.status, 10, 121, 300, 10);
    } else {
        for (size_t row = 0; row < model.description_lines.size() && row < 2; ++row) {
            const std::string &text = model.description_lines[row];
            label(root, text, 10, 120 + static_cast<int>(row) * 10,
                  model.description_page_count > 0 ? 268 : 300, 10,
                  context.font_for_text(text, &lv_font_montserrat_10), 0xB8B8B8, LV_LABEL_LONG_DOT);
        }
        if (model.description_page_count > 0)
            center_label(root, std::to_string(model.description_position) + "/" +
                         std::to_string(model.description_page_count), 280, 130, 34, 10,
                         &lv_font_montserrat_10, 0x6E7681, LV_LABEL_LONG_DOT);
    }
    if (model.job_running) {
        box(root, 10, 135, 300, 5, 0x30363D, 0x30363D, 0);
        if (model.job_progress >= 0)
            box(root, 10, 135, std::max(2, std::min(300, model.job_progress * 3)), 5,
                0xCCCC33, 0xCCCC33, 0);
        else
            box(root, 10 + static_cast<int>((lv_tick_get() / 120) % 260), 135, 40, 5,
                0xCCCC33, 0xCCCC33, 0);
        label(root, appstore::one_line(model.status.empty() ? "Working..." : model.status, 54),
              10, 143, 300, 12, &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
        return;
    }
    draw_shortcuts(model.app);
}

void AppDetailPage::render_confirmation(const PageRenderContext &context,
                                        const ConfirmationViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(root, 22, 37, 276, 105, 0x111923, 0x58A6FF, 2, 3);
    box(root, 22, 37, 276, 23, 0x1F6FEB, 0x1F6FEB, 0);
    center_strong_label(root, model.lines.empty() || model.lines[0] != "FILE OWNERSHIP CONFLICT"
                        ? "CONFIRM ACTION" : "FORCE OVERWRITE?", 42, 42, 236, 15,
                        &lv_font_montserrat_12, 0xFFFFFF);
    int y = 68;
    for (const auto &line_text : model.lines) {
        label(root, appstore::one_line(line_text, 42), 35, y, 250, 12,
              &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
        y += 14;
        if (y > 109) break;
    }
    const bool yes = model.focus == 0;
    const bool no = model.focus == 1;
    box(root, 54, 119, 84, 17, yes ? 0x2EA043 : 0x1A6A2A,
        yes ? 0xCCCC33 : 0x2EA043, yes ? 2 : 1);
    center_strong_label(root, "Y YES", 58, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    box(root, 182, 119, 84, 17, no ? 0x8B2F34 : 0x4B1F24,
        no ? 0xCCCC33 : 0xF85149, no ? 2 : 1);
    center_strong_label(root, "N NO", 186, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    center_strong_label(root, "Left/Right or Tab select   Enter confirm", 28, 149, 264, 12,
                        &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void AppDetailPage::render_progress(const PageRenderContext &context,
                                    const PackageProgressViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(root, 0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(root, "Package Operation", 8, 24, 190, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(root, "Esc Exit", 262, 24, 52, 14, &lv_font_montserrat_10, 0xAECBFA);
    center_strong_label(root, appstore::upper_ascii(model.action), 24, 50, 272, 18,
                        &lv_font_montserrat_14, 0xCCCC33, LV_LABEL_LONG_DOT);
    const std::string title = appstore::one_line(model.title, 26);
    center_strong_label(root, title, 24, 70, 272, 16,
                        context.font_for_text(title, &lv_font_montserrat_12),
                        0xE6EDF3, LV_LABEL_LONG_DOT);
    box(root, 28, 92, 264, 12, 0x30363D, 0x30363D, 0, 2);
    if (model.progress >= 0) {
        const int fill = std::max(4, std::min(264, model.progress * 264 / 100));
        box(root, 28, 92, fill, 12, 0xCCCC33, 0xCCCC33, 0, 2);
    } else {
        const int offset = static_cast<int>((lv_tick_get() / 90) % 216);
        box(root, 28 + offset, 92, 48, 12, 0xCCCC33, 0xCCCC33, 0, 2);
    }
    center_label(root, appstore::one_line(model.detail, 48), 18, 113, 284, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    center_strong_label(root, "Elapsed " + std::to_string(model.elapsed_seconds) + "s",
                        88, 132, 144, 13, &lv_font_montserrat_10, 0x58A6FF,
                        LV_LABEL_LONG_DOT);
    center_label(root, "Keep AppStore open until this finishes.", 18, 153, 284, 12,
                 &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void AppDetailPage::render_error(const PageRenderContext &context,
                                 const ErrorDialogViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(root, 18, 34, 284, 116, 0x111923, 0xF85149, 2, 4);
    box(root, 18, 34, 284, 23, 0x8B2F34, 0x8B2F34, 0);
    center_strong_label(root, model.title, 32, 39, 256, 15,
                        &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
    center_strong_label(root, appstore::one_line(model.message, 38), 30, 68, 260, 15,
                        &lv_font_montserrat_12, 0xFFD2D2, LV_LABEL_LONG_DOT);
    int y = 89;
    for (size_t i = 0; i < model.detail_lines.size() && i < 3; ++i) {
        center_label(root, appstore::one_line(model.detail_lines[i], 44), 30, y, 260, 12,
                     &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
        y += 13;
    }
    box(root, 118, 128, 84, 17, 0x2EA043, 0xCCCC33, 2, 2);
    center_strong_label(root, model.repairable ? "REPAIR" : "OK", 122, 131, 76, 12,
                        &lv_font_montserrat_10, 0xFFFFFF);
    center_label(root, model.repairable ? "Enter Repair  B Back" : "Enter OK",
                 model.repairable ? 82 : 102, 154, model.repairable ? 156 : 116, 12,
                 &lv_font_montserrat_10, 0xCCCC33);
}

void AppDetailPage::render_screenshot_overlay(const PageRenderContext &context,
                                              const ScreenshotOverlayViewModel &model,
                                              const std::function<void()> &draw_background)
{
    context.prepare();
    lv_obj_t *root = screen();
    box(root, 0, 0, 320, 170, 0x05070A, 0x05070A, 0);
    if (!model.has_app) {
        box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
        label(root, "No selected app", 10, 50, 304, 14, &lv_font_montserrat_12, 0xE6EDF3);
        return;
    }
    if (model.has_screenshots) draw_background();
    if (!model.has_screenshots || model.overlay_visible) {
        box(root, 0, 0, 320, 23, 0x05070A, 0x05070A, 0, 0, static_cast<lv_opa_t>(210));
        box(root, 0, 145, 320, 25, 0x05070A, 0x05070A, 0, 0, static_cast<lv_opa_t>(210));
        label(root, "Screenshots", 8, 5, 120, 14, &lv_font_montserrat_12, 0xFFFFFF);
        label(root, "Esc Back", 258, 5, 56, 14, &lv_font_montserrat_10, 0xAECBFA);
    }
    if (model.loading) {
        lv_obj_t *spinner = lv_spinner_create(root);
        lv_obj_set_size(spinner, 34, 34);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -8);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x30363D), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x58A6FF), LV_PART_INDICATOR);
        center_label(root, "Loading screenshots...", 52, 108, 216, 14,
                     &lv_font_montserrat_12, 0xAECBFA, LV_LABEL_LONG_DOT);
        return;
    }
    if (!model.has_screenshots) {
        center_strong_label(root, model.load_failed ? "LOAD FAILED" : "NO SCREENSHOTS", 52, 76, 216, 16,
                            &lv_font_montserrat_14, 0xCCCC33, LV_LABEL_LONG_DOT);
        return;
    }
    if (!model.overlay_visible) return;
    center_strong_label(root, std::to_string(model.image_index + 1) + "/" +
                        std::to_string(model.image_count), 136, 5, 48, 14,
                        &lv_font_montserrat_10, 0xAECBFA, LV_LABEL_LONG_DOT);
    center_label(root, "Z / < previous        C / > next", 36, 153, 248, 12,
                 &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void StoreSettingsPage::render(const PageRenderContext &context,
                               const StoreSettingsViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(root, 0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(root, "Registry Settings", 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(root, "B Back", 270, 24, 44, 14, &lv_font_montserrat_10, 0xAECBFA);
    const bool region_focused = model.focus == 0;
    label(root, "REGION", 24, 45, 45, 12, &lv_font_montserrat_10,
          region_focused ? 0xCCCC33 : 0x58A6FF);
    radio_option(root, 76, 45, "Auto", model.region_code == "auto", region_focused);
    radio_option(root, 142, 45, "Default", model.region_code == "default", region_focused);
    radio_option(root, 234, 45, "CN", model.region_code == "CN", region_focused);
    if (model.region_code == "auto")
        label(root, "using " + model.active_region, 248, 58, 60, 12,
              &lv_font_montserrat_10, 0x8B949E, LV_LABEL_LONG_DOT);
    if (!model.has_entry) {
        label(root, model.loading ? "Loading registries..." : "No registries configured.",
              10, 82, 300, 14, &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    } else {
        const auto &entry = model.entry;
        center_label(root, std::to_string(model.selected_index + 1) + "/" +
                     std::to_string(model.entry_count), 250, 45, 42, 12,
                     &lv_font_montserrat_10, 0x8B949E, LV_LABEL_LONG_DOT);
        const uint32_t nav = model.focus == 1 ? 0xFF6A3D : 0x6E7681;
        strong_label(root, "<", 8, 88, 12, 18, &lv_font_montserrat_20, nav);
        strong_label(root, ">", 303, 88, 12, 18, &lv_font_montserrat_20, nav);
        label(root, "NAME", 24, 62, 34, 12, &lv_font_montserrat_10,
              model.focus == 1 ? 0xCCCC33 : 0x58A6FF);
        strong_label(root, appstore::one_line(entry.name, 28), 24, 75, 210, 16,
                     &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
        label(root, entry.enabled ? "on" : "off", 242, 75, 28, 12,
              &lv_font_montserrat_10, entry.enabled ? 0x43CF4D : 0x6E7681, LV_LABEL_LONG_DOT);
        label(root, entry.count + " apps", 272, 75, 42, 12,
              &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
        label(root, entry.builtin ? "URL (region)" : "URL", 24, 96, 80, 12,
              &lv_font_montserrat_10, 0x58A6FF);
        box(root, 24, 109, 272, 26, 0x111923, 0x2A3A46, 1, 2);
        label(root, entry.builtin ? model.registry_url : entry.url, 30, 112, 260, 20,
              &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_WRAP);
        if (!entry.error.empty())
            label(root, appstore::one_line(entry.error, 45), 24, 136, 272, 12,
                  &lv_font_montserrat_10, 0xF85149, LV_LABEL_LONG_DOT);
    }
    const std::string hint = model.region_commit_pending ? "Region update pending..." :
        (model.operation_running ? "Registry operation running..." :
         (region_focused ? "A add registry  Up/Down focus  < > region" :
          "A add registry  E edit  T toggle  D del  R sync"));
    label(root, hint, 10, 153, 300, 12, &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void StoreSettingsPage::render_editor(const PageRenderContext &context,
                                      const RegistryEditorViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(root, 0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(root, model.editing ? "Edit Registry" : "Add Registry",
          8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);

    label(root, "NAME", 10, 46, 50, 12, &lv_font_montserrat_10,
          model.focus == 0 ? 0xCCCC33 : 0x58A6FF);
    box(root, 10, 59, 300, 24, 0x111923,
        model.focus == 0 ? 0xCCCC33 : 0x2A3A46, 1, 2);
    label(root, appstore::one_line(model.name, 34), 16, 64, 288, 14,
          &lv_font_montserrat_12, 0xE6EDF3, LV_LABEL_LONG_DOT);

    label(root, "URL", 10, 88, 50, 12, &lv_font_montserrat_10,
          model.focus == 1 ? 0xCCCC33 : 0x58A6FF);
    box(root, 10, 101, 300, 42, 0x111923,
        model.focus == 1 ? 0xCCCC33 : 0x2A3A46, 1, 2);
    label(root, model.url, 16, 105, 288, 34, &lv_font_montserrat_12,
          0xE6EDF3, LV_LABEL_LONG_WRAP);

    label(root, "Up/Down focus  Enter save", 10, 153, 166, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    label(root, "Backspace del  Esc back", 176, 153, 134, 12,
          &lv_font_montserrat_10, 0x8B949E, LV_LABEL_LONG_DOT);
}

} // namespace appstore_ui
