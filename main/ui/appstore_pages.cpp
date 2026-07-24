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
    if (mode == LV_LABEL_LONG_SCROLL_CIRCULAR)
        lv_obj_set_style_anim_duration(obj, 4000, LV_PART_MAIN | LV_STATE_DEFAULT);
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

void right_strong_label(lv_obj_t *root, const std::string &text, int x, int y,
                        int w, int h, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *first = label(root, text, x, y, w, h, font, color);
    lv_obj_set_style_text_align(first, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *second = label(root, text, x + 1, y, w - 1, h, font, color);
    lv_obj_set_style_text_align(second, LV_TEXT_ALIGN_RIGHT, 0);
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

void set_description_y(void *object, int32_t y)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(object), y);
}

std::string joined_description(const std::vector<std::string> &lines, int start)
{
    if (lines.empty()) return "-";
    std::string text;
    for (size_t offset = 0; offset < lines.size(); ++offset) {
        const size_t index = (static_cast<size_t>(std::max(0, start)) + offset) % lines.size();
        if (!text.empty()) text += '\n';
        text += lines[index];
    }
    return text;
}

void description_view(lv_obj_t *root, const AppDetailViewModel &model,
                      const lv_font_t *font)
{
    constexpr int kLineSpace = 5;
    constexpr int kVisibleLines = 3;
    constexpr int kContentX = 50;
    constexpr int kContentY = 87;
    constexpr int kContentWidth = 260;
    const int viewport_height =
        kVisibleLines * font->line_height + (kVisibleLines - 1) * kLineSpace;
    lv_obj_t *viewport = lv_obj_create(root);
    lv_obj_remove_style_all(viewport);
    lv_obj_set_pos(viewport, kContentX, kContentY);
    lv_obj_set_size(viewport, kContentWidth, viewport_height);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);

    const std::string description = joined_description(
        model.description_lines, model.description_start);
    const bool loops = model.description_lines.size() > kVisibleLines;
    const std::string separator = "------------------------------";
    const std::string block = description + "\n" + separator;
    const std::string text = loops
        ? block + "\n" + block + "\n" + block
        : description;

    lv_obj_t *content = lv_label_create(viewport);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_width(content, kContentWidth);
    lv_obj_set_height(content, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(content, font, 0);
    lv_obj_set_style_text_color(content, lv_color_hex(0xB8B8B8), 0);
    lv_obj_set_style_text_letter_space(content, 0, 0);
    lv_obj_set_style_text_line_space(content, kLineSpace, 0);
    lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
    lv_label_set_text(content, text.c_str());
    if (!loops) return;

    const int block_lines = static_cast<int>(model.description_lines.size()) + 1;
    const int block_height = block_lines * (font->line_height + kLineSpace);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, content);
    lv_anim_set_values(&animation, 0, -block_height);
    lv_anim_set_duration(&animation, std::max(4000, block_lines * 1100));
    lv_anim_set_delay(&animation, 1400);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_set_exec_cb(&animation, set_description_y);
    lv_anim_start(&animation);
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
    set_page_title("Store");
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
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
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
                        (preparing_catalog ? "Building app list...   Tab: Settings" :
                         "Tab: Settings   Hold ESC: Exit"), 18, 151, 284, 12,
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
    center_label(root, "Tab: Settings   Enter confirm", 48, 146, 224, 12,
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
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    draw_category();
    draw_icon();
    if (model.show_empty) {
        strong_label(root, "NO APPS", 106, 76, 130, 24, &lv_font_montserrat_20, 0xFFFFFF);
    } else {
        const int y_pos[] = {43, 59, 74, 108, 123};
        const lv_font_t *fonts[] = {&lv_font_montserrat_10, &lv_font_montserrat_12,
            &lv_font_montserrat_20, &lv_font_montserrat_12, &lv_font_montserrat_10};
        const uint32_t colors[] = {0x3D3D3D, 0x575757, 0xFFFFFF, 0x575757, 0x3D3D3D};
        for (const auto &row : model.rows) {
            const std::string text = appstore::upper_ascii(row.name);
            const lv_font_t *font = context.font_for_text(text, fonts[row.row]);
            const lv_label_long_mode_t name_mode =
                row.selected && appstore::utf8_display_width(text) > 12
                    ? LV_LABEL_LONG_SCROLL_CIRCULAR
                    : LV_LABEL_LONG_CLIP;
            strong_label(root, text, 106, y_pos[row.row], row.selected ? 144 : 130,
                         row.selected ? 24 : 16, font, colors[row.row],
                         name_mode);
            if (row.selected) {
                strong_label(root, "V" + appstore::one_line(row.version.empty() ? "0" : row.version, 6),
                             264, y_pos[row.row] + 3, 52, 16, &lv_font_montserrat_14,
                             0x8B8B8B, LV_LABEL_LONG_DOT);
                const std::string author = row.author.empty() ? "-" : row.author;
                strong_label(root, author,
                             109, y_pos[row.row] + 22, 190, 12,
                             &lv_font_montserrat_10, 0xBDBDBD,
                             appstore::utf8_display_width(author) > 28
                                 ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
            }
        }
        right_strong_label(root, std::to_string(model.selected_index + 1) + "/" +
                           std::to_string(model.app_count), 249, 123, 58, 15,
                           &lv_font_montserrat_14, 0xFFFFFF);
    }
    if (model.show_status) scrolling_status_label(root, model.status, 106, 132, 172, 8);
    draw_shortcuts();
}

void CatalogDisplayPage::render_text_entry(const PageRenderContext &context,
                                           const TextEntryViewModel &model)
{
    context.prepare();
    context.draw_system_bar();
    lv_obj_t *root = screen();
    box(root, 0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    strong_label(root, appstore::upper_ascii(model.title), 10, 27, 210, 17,
                 &lv_font_montserrat_14, model.accent, LV_LABEL_LONG_DOT);
    label(root, "TEXT INPUT", 238, 29, 72, 12,
          &lv_font_montserrat_10, 0x6E7681, LV_LABEL_LONG_DOT);
    center_strong_label(root, model.prompt, 58, 49, 204, 16,
                        &lv_font_montserrat_12, model.accent, LV_LABEL_LONG_DOT);
    box(root, model.input_x, 67, model.input_width, 43, 0x111923, model.accent, 2, 3);
    const std::string display = model.input.empty() ? model.placeholder :
        appstore::upper_ascii(model.input);
    center_strong_label(root, appstore::one_line(display, model.display_limit),
                        model.input_x + 10, 78, model.input_width - 20, 23,
                        &lv_font_montserrat_20,
                        model.input.empty() ? 0x6E7681 : 0xE6EDF3, LV_LABEL_LONG_DOT);
    center_label(root, appstore::one_line(model.message, 48), 10, 118, 300, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    center_label(root, "Esc Back   BS Delete   Enter Confirm", 10, 153, 300, 12,
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
    strong_label(root, "SEARCH RESULTS", 10, 25, 190, 17,
                 &lv_font_montserrat_14, 0x7ED957, LV_LABEL_LONG_DOT);
    right_strong_label(root, std::to_string(model.result_count) + " APPS",
                       230, 27, 80, 13, &lv_font_montserrat_10, 0xB8B8B8);
    box(root, 10, 44, 300, 18, 0x111923, 0x2EA043, 1, 3);
    label(root, "QUERY", 17, 48, 42, 11, &lv_font_montserrat_10, 0x7ED957);
    label(root, appstore::upper_ascii(model.input), 62, 47, 240, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_SCROLL_CIRCULAR);
    for (size_t row = 0; row < model.rows.size(); ++row) {
        const auto &item = model.rows[row];
        const int y = 66 + static_cast<int>(row) * 27;
        box(root, 10, y, 300, 24, item.selected ? 0x142817 : 0x10171D,
            item.selected ? 0x7ED957 : 0x27313A, 1, 3);
        const std::string name = appstore::upper_ascii(item.name);
        strong_label(root, name, 17, y + 2, 218, 14,
                     context.font_for_text(name, item.selected ? &lv_font_montserrat_12 :
                                           &lv_font_montserrat_10),
                     item.selected ? 0xFFFFFF : 0xB8B8B8,
                     item.selected && appstore::utf8_display_width(name) > 24
                         ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
        label(root, appstore::one_line(item.meta.empty() ? "-" : item.meta, 28),
              17, y + 14, 218, 9, &lv_font_montserrat_10,
              item.selected ? 0x7ED957 : 0x6E7681,
              LV_LABEL_LONG_DOT);
        lv_obj_t *version = label(
            root, "V" + appstore::one_line(item.version.empty() ? "0" : item.version, 7),
            239, y + 6, 63, 12, &lv_font_montserrat_10,
            item.selected ? 0xFFFFFF : 0x8B949E, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_RIGHT, 0);
    }
    center_label(root, "F/X Select   BS Edit   Enter Open   Esc Back",
                 10, 153, 300, 12, &lv_font_montserrat_10, 0xCCCC33,
                 LV_LABEL_LONG_DOT);
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
    lv_obj_t *title = label(root, model.title, 8, 24, 304, 16,
          context.font_for_text(model.title, &lv_font_montserrat_12), 0xFFFFFF,
          LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(title, 5000, LV_PART_MAIN | LV_STATE_DEFAULT);
    label(root, "STATE", 10, 48, 46, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(root, model.state, 62, 47, 94, 14, &lv_font_montserrat_12,
          model.app.installed ? 0xCCCC33 : 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(root, "SIZE", 164, 48, 42, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(root, model.app.size, 212, 47, 98, 14, &lv_font_montserrat_12,
          0xE6EDF3, LV_LABEL_LONG_DOT);
    label(root, "UPDATED", 10, 67, 46, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(root, model.updated, 62, 66, 94, 14,
          context.font_for_text(model.updated, &lv_font_montserrat_12), 0xE6EDF3,
          LV_LABEL_LONG_DOT);
    label(root, "AUTHOR", 164, 67, 42, 12, &lv_font_montserrat_10, 0x58A6FF);
    const std::string author = model.app.author.empty() ? "-" : model.app.author;
    label(root, author, 212, 66, 98, 14,
          context.font_for_text(author, &lv_font_montserrat_12), 0xE6EDF3,
          appstore::utf8_display_width(author) > 15
              ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
    label(root, "DESC", 10, 88, 38, 12, &lv_font_montserrat_10, 0x58A6FF);
    if (model.show_status) {
        box(root, 50, 87, 260, 46, 0x0D1117, 0x0D1117, 0);
        lv_obj_t *status = label(root, model.status, 50, 87, 260, 46,
                                 &lv_font_montserrat_12, 0xCCCC33,
                                 LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_anim_duration(status, 4000, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        const std::string description = joined_description(
            model.description_lines, model.description_start);
        description_view(root, model,
                         context.font_for_text(description, &lv_font_montserrat_12));
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
    const bool ownership_conflict =
        !model.lines.empty() && model.lines[0] == "FILE OWNERSHIP CONFLICT";
    const bool safety_warning = model.lines.size() >= 4 &&
        model.lines[1].find("cannot verify") != std::string::npos;
    const uint32_t accent = ownership_conflict || safety_warning ? 0xF85149 : 0x58A6FF;
    const uint32_t header = ownership_conflict || safety_warning ? 0x8B2F34 : 0x1F6FEB;
    box(root, 18, 30, 284, 116, 0x111923, accent, 2, 4);
    box(root, 18, 30, 284, 23, header, header, 0);
    center_strong_label(root, ownership_conflict ? "FORCE OVERWRITE?" :
                        (safety_warning ? "SAFETY WARNING" : "CONFIRM ACTION"),
                        38, 35, 244, 15,
                        &lv_font_montserrat_12, 0xFFFFFF);
    int y = 60;
    for (size_t index = 0; index < model.lines.size(); ++index) {
        const std::string &line_text = model.lines[index];
        const bool action_line = index == 0 && !ownership_conflict;
        center_strong_label(root, appstore::one_line(line_text, 44),
                            28, y, 264, action_line ? 15 : 12,
                            action_line ? &lv_font_montserrat_12 : &lv_font_montserrat_10,
                            index >= 1 && safety_warning ? 0xFFD2D2 : 0xE6EDF3,
                            LV_LABEL_LONG_DOT);
        y += action_line ? 18 : 14;
        if (y > 111) break;
    }
    const bool yes = model.focus == 0;
    const bool no = model.focus == 1;
    box(root, 62, 122, 84, 17, yes ? 0x2EA043 : 0x1A6A2A,
        yes ? 0xCCCC33 : 0x2EA043, yes ? 2 : 1);
    center_strong_label(root, "YES", 66, 125, 76, 12,
                        &lv_font_montserrat_10, 0xFFFFFF);
    box(root, 174, 122, 84, 17, no ? 0x8B2F34 : 0x4B1F24,
        no ? 0xCCCC33 : 0xF85149, no ? 2 : 1);
    center_strong_label(root, "NO", 178, 125, 76, 12,
                        &lv_font_montserrat_10, 0xFFFFFF);
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
    center_label(root, "Keep Store open until this finishes.", 18, 153, 284, 12,
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
    label(root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);
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
        label(root, "SOURCE", 24, 62, 48, 12, &lv_font_montserrat_10,
              model.focus == 1 ? 0xCCCC33 : 0x58A6FF);
        strong_label(root, appstore::one_line(entry.name, 28), 24, 75, 210, 16,
                     &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
        label(root, entry.count + " apps", 270, 75, 44, 12,
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
    if (model.region_commit_pending)
        label(root, "Region update pending...", 10, 138, 300, 12,
              &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    else if (model.operation_running)
        label(root, "Registry operation running...", 10, 138, 300, 12,
              &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
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

    center_label(root, "Esc Back   Tab Next   BS Delete   Enter Save",
                 10, 153, 300, 12, &lv_font_montserrat_10, 0xCCCC33,
                 LV_LABEL_LONG_DOT);
}

} // namespace appstore_ui
