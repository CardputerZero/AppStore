#pragma once

#include "cp0_keyboard_input_context.hpp"

#include "appstore_image_renderer.hpp"
#include "appstore_input_controller.hpp"
#include "appstore_lifecycle.hpp"
#include "appstore_lvgl_event_adapter.hpp"
#include "appstore_low_battery_overlay.hpp"
#include "appstore_pages.hpp"
#include "appstore_presenter.hpp"
#include "appstore_refresh_coordinator.hpp"
#include "appstore_request_coordinator.hpp"
#include "appstore_runtime_state.hpp"
#include "appstore_session_state.hpp"
#include "appstore_task_coordinator.hpp"
#include "appstore_timer_service.hpp"
#include "appstore_view_model_factory.hpp"
#include "catalog_controller.hpp"
#include "detail_action_controller.hpp"
#include "exit_controller.hpp"
#include "package_job_service.hpp"
#include "package_operation_controller.hpp"
#include "registry_controller.hpp"
#include "share_code_state.hpp"
#include "sudo_package_runner.hpp"
#include "sync_controller.hpp"
#include "system_status_controller.hpp"
#include "system_status_state.hpp"

namespace appstore_ui {

class AppStoreApp : public InitializationProgressPage
{
public:
    AppStoreApp();
    ~AppStoreApp() override;

    static void set_arguments(int argc, char **argv);
    static AppStoreRuntimeState &runtime() { return runtime_; }
    static AppStoreApp *current() { return current_; }

    AppStoreLifecycle &lifecycle() { return lifecycle_; }

    void activate(Screen screen);
    void handle_key(const AppStoreKeyEvent &key);
    void render_current_screen();
    bool poll_registry_refresh();
    bool poll_registry_operation();
    bool poll_screenshots();
    void poll_sync();
    void refresh();
    void poll_package_job();

private:
    void bind_page(AppStoreUiPage &page);
    void build_ui();
    void draw_system_bar();
    void set_active_page(AppStoreUiPage &page);
    void navigate_back();
    void open_registry_screen();
    void open_registry_add_screen();
    void execute_detail_confirmation();
    void repair_package_error();

    static int startup_argc_;
    static char **startup_argv_;
    static AppStoreRuntimeState runtime_;
    static AppStoreApp *current_;

    Cp0KeyboardInputContextScope input_context_scope_;
    AppStoreLowBatteryOverlay low_battery_overlay_;
    SystemStatusController status_controller_;
    DetailActionController detail_actions_;
    RegistryController registry_actions_;
    ShareCodeState share_code_;
    CatalogController catalog_actions_;
    AppStoreRequestCoordinator request_coordinator_;
    SyncController sync_actions_;
    AppStoreInputController input_controller_;
    AppStoreLvglEventAdapter event_adapter_;
    SudoPackageRunner sudo_runner_;
    PackageOperationController package_operations_;
    AppStoreRefreshCoordinator refresh_coordinator_;
    AppStoreViewModelFactory view_models_;
    CatalogDisplayPage catalog_page_;
    AppDetailPage detail_page_;
    StoreSettingsPage settings_page_;
    AppStorePresenter presenter_;
    AppStoreTimerService timers_;
    AppStoreLifecycle lifecycle_;
    AppStoreUiPage *active_page_ = nullptr;
};

} // namespace appstore_ui
