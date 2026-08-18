#include "appstore_presenter.hpp"

#include "appstore_fonts.hpp"
#include "appstore_paths.hpp"
#include "appstore_shortcut_bar.hpp"

#include <cctype>

namespace appstore_ui {
namespace {

constexpr int kHomeIconX = 10;
constexpr int kHomeIconY = 49;
constexpr int kHomeIconSize = 68;
constexpr uint32_t kStatusVisibleMs = 6000;

lv_obj_t *label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                const lv_font_t *font, uint32_t color,
                lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font_for_text(text, font), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_label_set_long_mode(obj, mode);
    lv_label_set_text(obj, text.c_str());
    if (mode == LV_LABEL_LONG_SCROLL_CIRCULAR)
        lv_obj_set_style_anim_duration(obj, 4000, LV_PART_MAIN | LV_STATE_DEFAULT);
    return obj;
}

void strong_label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                  const lv_font_t *font, uint32_t color,
                  lv_label_long_mode_t mode = LV_LABEL_LONG_DOT)
{
    label(parent, text, x, y, w, h, font, color, mode);
    label(parent, text, x + 1, y, w, h, font, color, mode);
}

void center_strong_label(lv_obj_t *parent, const std::string &text, int x, int y,
                         int w, int h, const lv_font_t *font, uint32_t color,
                         lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    lv_obj_t *first = label(parent, text, x, y, w, h, font, color, mode);
    lv_obj_set_style_text_align(first, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *second = label(parent, text, x + 1, y, w, h, font, color, mode);
    lv_obj_set_style_text_align(second, LV_TEXT_ALIGN_CENTER, 0);
}

void box(lv_obj_t *root, int x, int y, int w, int h, uint32_t color,
         uint32_t border, int border_width, int radius)
{
    lv_obj_t *obj = lv_obj_create(root);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
}

void transparent_frame(lv_obj_t *root, int x, int y, int w, int h,
                       uint32_t border, int border_width, int radius)
{
    lv_obj_t *obj = lv_obj_create(root);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

std::string app_initial(const appstore::StoreApp &app)
{
    for (char ch : app.name) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (std::isalnum(value))
            return std::string(1, static_cast<char>(std::toupper(value)));
    }
    return "A";
}

} // namespace

AppStorePresenter::AppStorePresenter(
    AppStoreSessionState &session, CatalogController &catalog,
    AppStoreViewModelFactory &view_models,
    InitializationProgressPage &initialization_page,
    CatalogDisplayPage &catalog_page, AppDetailPage &detail_page,
    StoreSettingsPage &settings_page, AppStoreImageRenderer &images,
    std::function<void()> draw_system_bar)
    : session_(session), catalog_(catalog), view_models_(view_models),
      initialization_page_(initialization_page), catalog_page_(catalog_page),
      detail_page_(detail_page), settings_page_(settings_page), images_(images),
      draw_system_bar_(std::move(draw_system_bar))
{
}

PageRenderContext AppStorePresenter::context(AppStoreUiPage &page)
{
    return {[this, &page]() { prepare(page); }, draw_system_bar_,
            [](const std::string &text, const lv_font_t *font) {
                return font_for_text(text, font);
            }};
}

void AppStorePresenter::prepare(AppStoreUiPage &page)
{
    page.clear_content();
    lv_obj_t *root = page.screen();
    images_.begin_frame();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
}

void AppStorePresenter::draw_category_selector(lv_obj_t *root)
{
    if (!images_.draw_packaged(root, "store_arrow_left.png", 197, 34))
        strong_label(root, "<", 198, 32, 14, 18, &lv_font_montserrat_20, 0xFF6A3D);
    const std::string category = appstore::upper_ascii(session_.catalog.current_category_name());
    const lv_label_long_mode_t mode = appstore::utf8_display_width(category) > 8
        ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP;
    center_strong_label(root, category, 212, 32, 82, 16,
                        &lv_font_montserrat_14, 0xFFFFFF, mode);
    if (!images_.draw_packaged(root, "store_arrow_right.png", 298, 34))
        strong_label(root, ">", 300, 32, 14, 18, &lv_font_montserrat_20, 0xFF6A3D);
}

void AppStorePresenter::draw_home_icon_panel(lv_obj_t *root,
                                              const appstore::StoreApp *app)
{
    const int arrow_x = kHomeIconX + (kHomeIconSize - 14) / 2;
    if (!images_.draw_packaged(root, "store_arrow_up.png", arrow_x, 36))
        center_strong_label(root, "^", kHomeIconX + 20, 35, 28, 18,
                            &lv_font_montserrat_20, 0xFF6A3D);
    if (!app) {
        center_strong_label(root, "-", kHomeIconX + 22, kHomeIconY + 24, 24, 22,
                            &lv_font_montserrat_20, 0x9A9A9A);
    } else if (!images_.draw_app_icon(root, session_.app_dir, *app)) {
        center_strong_label(root, app_initial(*app), kHomeIconX, kHomeIconY + 21,
                            kHomeIconSize, 28, &lv_font_montserrat_20,
                            app->installed ? 0x52D05D : 0xFFFFFF);
    }
    transparent_frame(root, kHomeIconX, kHomeIconY, kHomeIconSize, kHomeIconSize,
                      0x6E7681, 2, 10);
    if (!images_.draw_packaged(root, "store_arrow_down.png", arrow_x, 123))
        center_strong_label(root, "v", kHomeIconX + 20, 123, 28, 18,
                            &lv_font_montserrat_20, 0xFF6A3D);
}

bool AppStorePresenter::draw_detail_background(lv_obj_t *root,
                                                const appstore::StoreApp &app)
{
    const auto screenshots = detail_screenshot_paths(session_.app_dir, app);
    session_.detail_media.normalize_images(app.id, static_cast<int>(screenshots.size()));
    return !screenshots.empty() &&
           images_.draw_screenshot(root, screenshots[session_.detail_media.image_index()]);
}

void AppStorePresenter::render(Screen screen, bool registry_operation_running)
{
    switch (screen) {
        case Screen::StartupSync:
            initialization_page_.render(context(initialization_page_), view_models_.initialization());
            break;
        case Screen::Home: {
            lv_obj_t *root = catalog_page_.screen();
            catalog_page_.render(
                context(catalog_page_), view_models_.catalog(lv_tick_get(), kStatusVisibleMs),
                [this, root]() { draw_category_selector(root); },
                [this, root]() { draw_home_icon_panel(root, session_.catalog.selected_app()); },
                [this, root]() {
                    AppStoreShortcutBar::render_catalog(
                        root, session_.catalog.selected_app());
                });
            break;
        }
        case Screen::Detail: {
            appstore::StoreApp *selected = catalog_.ensure_selected();
            detail_page_.render(
                context(detail_page_),
                view_models_.detail(selected, lv_tick_get(), kStatusVisibleMs),
                [this](const appstore::StoreApp &app) {
                    AppStoreShortcutBar::render_detail(
                        detail_page_.screen(), session_.app_dir, app);
                });
            break;
        }
        case Screen::Confirm:
            detail_page_.render_confirmation(context(detail_page_), view_models_.confirmation());
            break;
        case Screen::Progress:
            detail_page_.render_progress(context(detail_page_), view_models_.progress(lv_tick_get()));
            break;
        case Screen::ErrorDialog:
            detail_page_.render_error(context(detail_page_), view_models_.error());
            break;
        case Screen::Registry: {
            const StoreSettingsViewModel model =
                view_models_.settings(registry_operation_running);
            settings_page_.render(context(settings_page_), model);
            AppStoreShortcutBar::render_settings(
                settings_page_.screen(), model.has_entry,
                model.has_entry && model.entry.builtin,
                !model.loading && !model.operation_running &&
                    !model.region_commit_pending);
            break;
        }
        case Screen::RegistryEdit:
            settings_page_.render_editor(context(settings_page_), view_models_.registry_editor());
            break;
        case Screen::ShareCode:
            catalog_page_.render_text_entry(context(catalog_page_), view_models_.share_code());
            break;
        case Screen::Search: {
            const SearchPageViewModel model = view_models_.search();
            catalog_page_.render_search(context(catalog_page_), model);
            break;
        }
        case Screen::Screenshots: {
            appstore::StoreApp *app = catalog_.ensure_selected();
            std::vector<std::string> screenshots;
            if (app) {
                screenshots = detail_screenshot_paths(session_.app_dir, *app);
                session_.detail_media.normalize_images(app->id,
                                                       static_cast<int>(screenshots.size()));
            }
            session_.detail_media.hide_overlay_if_elapsed(lv_tick_get(), 2000);
            ScreenshotOverlayViewModel model;
            model.has_app = app != nullptr;
            model.has_screenshots = !screenshots.empty();
            model.overlay_visible = session_.detail_media.overlay_visible();
            model.image_index = session_.detail_media.image_index();
            model.image_count = static_cast<int>(screenshots.size());
            model.loading = session_.detail_media.loading();
            model.load_failed = session_.detail_media.load_failed();
            detail_page_.render_screenshot_overlay(
                context(detail_page_), model,
                [this, app]() { if (app) draw_detail_background(detail_page_.screen(), *app); });
            break;
        }
    }
}

} // namespace appstore_ui
