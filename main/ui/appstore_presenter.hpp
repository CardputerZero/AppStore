#pragma once

#include "appstore_image_renderer.hpp"
#include "appstore_pages.hpp"
#include "appstore_session_state.hpp"
#include "appstore_view_model_factory.hpp"
#include "catalog_controller.hpp"

#include <functional>

namespace appstore_ui {

class AppStorePresenter
{
public:
    AppStorePresenter(AppStoreSessionState &session, CatalogController &catalog,
                      AppStoreViewModelFactory &view_models,
                      InitializationProgressPage &initialization_page,
                      CatalogDisplayPage &catalog_page, AppDetailPage &detail_page,
                      StoreSettingsPage &settings_page, AppStoreImageRenderer &images,
                      std::function<void()> draw_system_bar);

    void render(Screen screen, bool registry_operation_running);

private:
    PageRenderContext context(AppStoreUiPage &page);
    void prepare(AppStoreUiPage &page);
    void draw_category_selector(lv_obj_t *root);
    void draw_home_icon_panel(lv_obj_t *root, const appstore::StoreApp *app);
    bool draw_detail_background(lv_obj_t *root, const appstore::StoreApp &app);

    AppStoreSessionState &session_;
    CatalogController &catalog_;
    AppStoreViewModelFactory &view_models_;
    InitializationProgressPage &initialization_page_;
    CatalogDisplayPage &catalog_page_;
    AppDetailPage &detail_page_;
    StoreSettingsPage &settings_page_;
    AppStoreImageRenderer &images_;
    std::function<void()> draw_system_bar_;
};

} // namespace appstore_ui
