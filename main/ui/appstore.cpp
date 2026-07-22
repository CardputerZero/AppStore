#include "lvgl/lvgl.h"
#include "main.h"

#include <stdint.h>
#include "appstore_app.hpp"
#include "appstore_client.hpp"
#include "appstore_fonts.hpp"
#include "appstore_paths.hpp"
#include "appstore_system_status_provider.hpp"
#include "input_keys.h"
#include "cp0_lvgl_app.h"
#include "cp0_lvgl_app_runner.hpp"
#include "hal_lvgl_bsp.h"

#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

void cp0_zmq_log_init(void);
void cp0_zmq_log(const char *topic, const char *message);

namespace appstore_ui {
int AppStoreApp::startup_argc_ = 0;
char **AppStoreApp::startup_argv_ = nullptr;
AppStoreRuntimeState AppStoreApp::runtime_;
AppStoreApp *AppStoreApp::current_ = nullptr;
} // namespace appstore_ui

namespace {

using namespace appstore;
using namespace appstore_ui;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr uint32_t kEscLongPressMs = 1200;
constexpr uint32_t kJobStartDelayMs = 80;
constexpr uint32_t kJobPollIntervalMs = 250;
constexpr uint32_t kTopStatusRefreshMs = 5000;
constexpr uint32_t kBatteryChargeAnimRefreshMs = 120;
constexpr uint32_t kLowBatteryFlashMs = 500;
constexpr uint32_t kSyncAnimRefreshMs = 200;
constexpr uint32_t kRegionDebounceMs = 2000;
constexpr uint32_t kStatusScrollVisibleMs = 6000;

AppStoreRuntimeState &g_runtime = AppStoreApp::runtime();

const char *screen_name(Screen screen)
{
    switch (screen) {
        case Screen::StartupSync: return "StartupSync";
        case Screen::Home: return "Home";
        case Screen::Detail: return "Detail";
        case Screen::Confirm: return "Confirm";
        case Screen::Progress: return "Progress";
        case Screen::ErrorDialog: return "ErrorDialog";
        case Screen::Registry: return "Registry";
        case Screen::RegistryEdit: return "RegistryEdit";
        case Screen::ShareCode: return "ShareCode";
        case Screen::Search: return "Search";
        case Screen::Screenshots: return "Screenshots";
    }
    return "Unknown";
}

void app_tracef(const char *fmt, ...)
{
    char buf[1024] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "[AppStore TRACE] %s\n", buf);
    cp0_zmq_log("appstore", buf);
}

struct TraceScope {
    const char *name;
    uint32_t start;
    bool always;

    TraceScope(const char *scope_name, bool log_always = false)
        : name(scope_name), start(lv_tick_get()), always(log_always)
    {
        if (always) app_tracef("%s begin screen=%s", name, screen_name(g_runtime.session.screen));
    }

    ~TraceScope()
    {
        uint32_t elapsed = lv_tick_elaps(start);
        if (always || elapsed >= 20) {
            app_tracef("%s end elapsed=%ums screen=%s", name, elapsed, screen_name(g_runtime.session.screen));
        }
    }
};

void request_quit()
{
    g_runtime.exit.request();
    cp0_lvgl_wake();
}

void handle_signal(int)
{
    request_quit();
}

class AppStoreSyncTopBarComponent final : public AppTopBarComponent
{
public:
    AppStoreSyncTopBarComponent()
        : AppTopBarComponent("appstore.sync", 24, 12)
    {
    }

    void set_sync_active(bool active, uint32_t tick)
    {
        set_visible(active);
        if (label_) {
            lv_obj_set_style_text_opa(label_,
                                      active && (tick / 200) % 4 < 2 ? 255 : 90,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

protected:
    void on_create(lv_obj_t *obj) override
    {
        label_ = lv_label_create(obj);
        lv_label_set_text(label_, "SYNC");
        lv_obj_set_size(label_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_text_font(label_, &lv_font_montserrat_10,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label_, lv_color_hex(0x33CC33),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(label_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                             LV_OBJ_FLAG_SCROLLABLE));
        set_visible(false);
    }

private:
    lv_obj_t *label_ = nullptr;
};

} // namespace

namespace appstore_ui {

bool AppStoreApp::poll_registry_refresh()
{
    return registry_actions_.poll_refresh();
}

bool AppStoreApp::poll_registry_operation()
{
    return registry_actions_.poll_operation();
}

void AppStoreApp::draw_system_bar()
{
    uint32_t start = lv_tick_get();
    status_controller_.refresh(false, start, kTopStatusRefreshMs);
    uint32_t status_ms = lv_tick_elaps(start);
    g_runtime.system_status.last_render_tick = lv_tick_get();

    bool sync_active = sync_actions_.running();
    if (!sync_active && g_runtime.session.sync.visible_until_tick != 0 &&
        lv_tick_elaps(g_runtime.session.sync.visible_until_tick) < 1000) {
        sync_active = true;
    }
    if (active_page_) {
        auto *sync_component = dynamic_cast<AppStoreSyncTopBarComponent *>(
            active_page_->top_bar_component("appstore.sync"));
        if (sync_component) sync_component->set_sync_active(sync_active, lv_tick_get());
    }
    const uint32_t elapsed = lv_tick_elaps(start);
    if (elapsed >= 15 || g_runtime.session.screen == Screen::Registry || g_runtime.session.screen == Screen::RegistryEdit) {
        app_tracef("draw_system_bar elapsed=%ums status=%ums screen=%s",
                   elapsed, status_ms, screen_name(g_runtime.session.screen));
    }
    return;

}

void AppStoreApp::render_current_screen()
{
    activate(g_runtime.session.screen);
    presenter_.render(g_runtime.session.screen, registry_actions_.operation_running());
}

void AppStoreApp::refresh()
{
    const uint32_t start = lv_tick_get();
    const RefreshPollResult result = refresh_coordinator_.poll(start);
    const bool screenshots = poll_screenshots();
    const uint32_t elapsed = lv_tick_elaps(start);
    if (elapsed >= 20 || result.any() || screenshots ||
        g_runtime.session.screen == Screen::Registry || g_runtime.session.screen == Screen::RegistryEdit) {
        app_tracef("refresh_timer elapsed=%ums screen=%s region_debounce=%d registry_refresh=%d registry_op=%d plan=%d summary=%d status_timeout=%d top_status=%d screenshots=%d",
                   elapsed, screen_name(g_runtime.session.screen), result.region_debounce ? 1 : 0,
                   result.registry_refresh ? 1 : 0, result.registry_operation ? 1 : 0,
                   result.plan ? 1 : 0, result.summary ? 1 : 0,
                   result.status_timeout ? 1 : 0, result.top_status ? 1 : 0,
                   result.screenshot_overlay ? 1 : 0);
    }
}

bool AppStoreApp::poll_screenshots()
{
    ScreenshotRequest request;
    ScreenshotResult result;
    if (!runtime_.task_service.take_screenshots(request, result)) return false;
    if (result.rc == 0) {
        appstore::StoreApp *app = runtime_.session.catalog.selected_app();
        if (app && app->id == request.app_id) {
            std::istringstream stream(result.output);
            std::string line;
            while (std::getline(stream, line)) {
                const auto fields = split_tab(line);
                if (fields.size() >= 3 && fields[0] == "SCREENSHOT") {
                    if (!app->images.empty()) app->images += ',';
                    app->images += fields[2];
                }
            }
        }
        request_coordinator_.request_summary();
    }
    else runtime_.session.status.value() = "Unable to load screenshots";
    runtime_.session.detail_media.finish_loading(request.app_id, result.rc != 0);
    render_current_screen();
    return true;
}

void AppStoreApp::poll_package_job()
{
    package_operations_.poll(lv_tick_get(), kJobStartDelayMs);
}

void AppStoreApp::navigate_back()
{
    switch (g_runtime.session.screen) {
        case Screen::StartupSync:
            // Exit from startup checking only via failure OK or long ESC.
            break;
        case Screen::Home:
            break;
        case Screen::Detail:
            g_runtime.session.screen = Screen::Home;
            break;
        case Screen::Confirm:
            g_runtime.session.screen = Screen::Detail;
            g_runtime.session.confirmation.reset();
            break;
        case Screen::Progress:
            if (g_runtime.package_jobs.state().running || g_runtime.package_jobs.state().pending_start) {
                if (g_runtime.package_jobs.state().phase == appstore_ui::PackageJobPhase::Sudo && g_runtime.package_jobs.state().sudo_request_id != 0) {
                    int rc = cp0_sudo_cancel(g_runtime.package_jobs.state().sudo_request_id);
                    if (rc == 0) {
                        g_runtime.package_jobs.state().cancel_requested = true;
                        g_runtime.session.status.value() = "Cancelling package operation...";
                    } else
                        g_runtime.session.status.value() = "Unable to cancel package operation";
                } else if (g_runtime.package_jobs.state().phase == appstore_ui::PackageJobPhase::Prepare) {
                    if (cancel_package_prepare()) {
                        g_runtime.package_jobs.state().cancel_requested = true;
                        g_runtime.session.status.value() = "Cancelling package preparation...";
                    } else {
                        g_runtime.session.status.value() = "Unable to cancel package preparation";
                    }
                } else {
                    g_runtime.session.status.value() = "Operation is still running";
                }
            } else {
                g_runtime.session.screen = Screen::Detail;
            }
            break;
        case Screen::ErrorDialog:
            g_runtime.session.error.clear();
            g_runtime.session.screen = Screen::Detail;
            break;
        case Screen::Registry:
            g_runtime.session.screen = g_runtime.session.sync.startup_active
                ? Screen::StartupSync : Screen::Home;
            break;
        case Screen::RegistryEdit:
            g_runtime.session.screen = Screen::Registry;
            break;
        case Screen::ShareCode:
            g_runtime.session.screen = Screen::Home;
            break;
        case Screen::Search:
            g_runtime.session.screen = Screen::Home;
            break;
        case Screen::Screenshots:
            g_runtime.session.screen = Screen::Detail;
            break;
    }
}

void AppStoreApp::poll_sync()
{
    sync_actions_.poll(lv_tick_get(), kSyncAnimRefreshMs);
}

void AppStoreApp::open_registry_screen()
{
    app_tracef("open_registry_screen begin from=%s entries=%zu loading=%d refresh_running=%d",
               screen_name(g_runtime.session.screen), g_runtime.session.registry.entries().size(), g_runtime.session.registry_loading ? 1 : 0,
               runtime_.task_service.registry_refresh_running() ? 1 : 0);
    registry_actions_.open();
    app_tracef("open_registry_screen end screen=%s", screen_name(g_runtime.session.screen));
}

void AppStoreApp::open_registry_add_screen()
{
    registry_actions_.open_add();
}

void AppStoreApp::execute_detail_confirmation()
{
    if (!detail_actions_.execute_confirmation(lv_tick_get())) return;
    app_tracef("job start action=%s app=%s title=%s",
               g_runtime.package_jobs.state().action.c_str(), g_runtime.package_jobs.state().app_id.c_str(),
               g_runtime.package_jobs.state().title.c_str());
    render_current_screen();
    lv_refr_now(nullptr);
}

void AppStoreApp::repair_package_error()
{
    if (!g_runtime.session.error.repairable ||
        g_runtime.session.error.repair_app_id.empty()) return;

    const std::string action = g_runtime.session.error.repair_action;
    const std::string app_id = g_runtime.session.error.repair_app_id;
    const std::string title = g_runtime.session.error.repair_title;
    g_runtime.session.error.clear();
    g_runtime.package_jobs.state().begin(action, app_id, title, lv_tick_get());
    g_runtime.package_jobs.state().phase = appstore_ui::PackageJobPhase::Repair;
    g_runtime.package_jobs.state().detail = "Repairing package transaction";
    g_runtime.session.status.value() = "Repairing package transaction...";
    g_runtime.session.screen = Screen::Progress;
    render_current_screen();
    lv_refr_now(nullptr);
}

void AppStoreApp::handle_key(const AppStoreKeyEvent &key)
{
    const uint32_t key_start = lv_tick_get();
    const Screen before_screen = g_runtime.session.screen;
    app_tracef("handle_key begin screen=%s code=%u ch=%d release=%d repeat=%d",
               screen_name(before_screen), key.code, static_cast<int>(key.ch),
               key.release ? 1 : 0, key.repeated ? 1 : 0);
    input_controller_.handle(key, key_start);
    app_tracef("handle_key end elapsed=%ums before=%s after=%s code=%u ch=%d",
               lv_tick_elaps(key_start), screen_name(before_screen), screen_name(g_runtime.session.screen),
               key.code, static_cast<int>(key.ch));
}

void AppStoreApp::build_ui()
{
    lv_obj_t *root = active_page_->screen();
    lv_obj_set_size(root, kScreenWidth, kScreenHeight);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
}

AppStoreApp::AppStoreApp()
    : status_controller_(
          runtime_.system_status,
          {[]() { return AppStoreSystemStatusProvider::read_wifi(); },
           []() { return AppStoreSystemStatusProvider::read_battery(); },
           []() { AppStoreSystemStatusProvider::shutdown(); },
           [this](SystemStatusState &state, uint32_t now, bool force) {
               low_battery_overlay_.update(state, now, force);
           },
           [this](SystemStatusState &state, uint32_t now, uint32_t interval) {
               low_battery_overlay_.animate(state, now, interval);
           }}),
      detail_actions_(runtime_.session, runtime_.package_jobs.state(),
                      [this](const std::string &action, const std::string &app_id) {
                          request_coordinator_.start_plan(action, app_id);
                      },
                      [this]() { return request_coordinator_.plan_running(); }),
      registry_actions_(
          runtime_.session, runtime_.task_service, runtime_.package_jobs.state(),
          {[](const std::string &fallback) { return load_registries(fallback); },
           []() { return g_runtime.exit.requested(); },
           [this]() { runtime_.registry_persistence.save_backend(); },
           [this]() { request_coordinator_.request_summary(); },
           [this](bool refresh) { sync_actions_.start(refresh, lv_tick_get()); },
           []() { stop_backend_service(); }}),
      catalog_actions_(runtime_.session, share_code_,
                       [this]() { request_coordinator_.request_summary(); }),
      request_coordinator_(
          runtime_.session, runtime_.task_service, catalog_actions_, detail_actions_,
          {[](SortRule rule) { return load_summary(rule); },
           []() { return g_runtime.exit.requested(); },
           [](const std::string &message) {
               std::fprintf(stderr, "[AppStore UI] %s\n", message.c_str());
           }}),
      sync_actions_(
          runtime_.session, runtime_.task_service,
          {[]() { return load_sync_status(); },
           []() { return cancel_sync(); },
           []() { return g_runtime.exit.requested(); },
           [this](SummaryPurpose purpose) { request_coordinator_.request_summary(purpose); },
           [this]() { registry_actions_.request_refresh(); },
           [this]() { render_current_screen(); },
           []() {
               return g_runtime.session.screen != Screen::Registry &&
                      g_runtime.session.screen != Screen::RegistryEdit;
           }}),
      input_controller_(
          runtime_.session, runtime_.package_jobs.state(), runtime_.exit, share_code_, catalog_actions_,
          detail_actions_, registry_actions_, sync_actions_,
          {[this]() { request_quit(); }, [this]() { navigate_back(); },
           [this]() { open_registry_screen(); }, [this]() { open_registry_add_screen(); },
           [this]() { execute_detail_confirmation(); },
           [this]() { repair_package_error(); },
           [this]() { render_current_screen(); }}),
      event_adapter_(
          [this](const AppStoreKeyEvent &key) { handle_key(key); },
          [this](const cp0_battery_info_t &info) {
              status_controller_.apply_battery(info, lv_tick_get());
          }),
      sudo_runner_(runtime_.package_jobs.state()),
      package_operations_(
          runtime_.session, runtime_.package_jobs, runtime_.workers, runtime_.exit,
          {[this]() { return sudo_runner_.start(backend_executable_path()); },
           [](uint64_t request_id) { return cp0_sudo_cancel(request_id); },
           [this]() { request_coordinator_.request_summary(); },
           [this]() { render_current_screen(); },
           []() { return g_runtime.exit.requested(); }}),
      refresh_coordinator_(
          {[this](uint32_t now) {
               status_controller_.tick_battery(now, kLowBatteryFlashMs);
           },
           [this](uint32_t now) {
               return registry_actions_.poll_region_debounce(now, kRegionDebounceMs);
           },
           [this]() { return poll_registry_refresh(); },
           [this]() { return poll_registry_operation(); },
           [this]() { return request_coordinator_.poll_plan(); },
           [this]() { return request_coordinator_.poll_summary(); },
           [](uint32_t now) {
               return !g_runtime.session.status.value().empty() &&
                      !g_runtime.session.status.visible(now, kStatusScrollVisibleMs);
           },
           [this](uint32_t now) {
               return status_controller_.top_status_render_due(
                   now, false,
                   kBatteryChargeAnimRefreshMs,
                   kTopStatusRefreshMs);
           },
           [](uint32_t now) {
               return g_runtime.session.screen == Screen::Screenshots &&
                      g_runtime.session.detail_media.hide_overlay_if_elapsed(now, 2000);
           },
           [this]() { render_current_screen(); }}),
      view_models_(runtime_.session, runtime_.package_jobs.state(), share_code_),
      presenter_(runtime_.session, catalog_actions_, view_models_, *this, catalog_page_,
                 detail_page_, settings_page_, runtime_.images, [this]() { draw_system_bar(); }),
      timers_({[this]() { refresh(); }, [this]() { poll_package_job(); },
               []() {
                   if (g_runtime.exit.consume_esc_hold(lv_tick_get(), kEscLongPressMs))
                       request_quit();
               },
               [this]() { poll_sync(); }}),
      lifecycle_(
          runtime_.session, runtime_.package_jobs.state(), runtime_.exit,
          {[this](int, char **argv) {
               runtime_.session.app_dir = dirname_of(argv && argv[0] ? argv[0] : nullptr);
               set_backend_executable_path(argv && argv[0] ? argv[0] :
                                           "M5CardputerZero-AppStore");
               std::fprintf(stderr, "[AppStore UI] ui_init argv0=%s app_dir=%s backend=%s\n",
                            argv && argv[0] ? argv[0] : "-", runtime_.session.app_dir.c_str(),
                            backend_executable_path().c_str());
               app_tracef("ui_init argv0=%s app_dir=%s backend=%s",
                          argv && argv[0] ? argv[0] : "-", runtime_.session.app_dir.c_str(),
                          backend_executable_path().c_str());
               if (!clear_cached_catalog())
                   app_tracef("registry cache clear failed");
               init_runtime_fonts(runtime_.session.app_dir);
               build_ui();
           },
           [this]() { status_controller_.reset(); },
           [this]() {
               status_controller_.refresh(true, lv_tick_get(), kTopStatusRefreshMs);
           },
           [this]() {
               sync_actions_.initialize_startup(runtime_.session.registry.registry_url());
           },
           [this]() { render_current_screen(); },
           [this]() {
               runtime_.registry_persistence.initialize();
               runtime_.session.sync.status.url = runtime_.session.registry.registry_url();
           },
           [this]() {
               timers_.start(250, kJobPollIntervalMs, 50, kSyncAnimRefreshMs);
           },
           [this]() { request_coordinator_.request_summary(); },
           [this]() { sync_actions_.start(false, lv_tick_get()); },
           [this]() { timers_.stop(); },
           [this]() { low_battery_overlay_.destroy(); },
           []() { g_runtime.system_status.low_battery.reset(); },
           []() { stop_backend_service(); },
           [this]() { return sync_actions_.running(); },
           [this]() { return sync_actions_.cancel(); },
           []() { return cancel_package_prepare(); },
           [](uint64_t request_id) { return cp0_sudo_cancel(request_id); },
           [this](const std::string &output, int rc) {
               package_operations_.finish(output, rc);
           },
           []() { return lv_tick_get(); }})
{
    detail_actions_.set_screenshot_starter([this](const std::string &app_id) {
        return runtime_.task_service.start_screenshots(app_id) == TaskStartResult::Started;
    });
    bind_page(*this);
    bind_page(catalog_page_);
    bind_page(detail_page_);
    bind_page(settings_page_);
    current_ = this;
    set_active_page(*this);
    ui_init(startup_argc_, startup_argv_);
}

AppStoreApp::~AppStoreApp()
{
    ui_deinit();
    current_ = nullptr;
}

void AppStoreApp::set_arguments(int argc, char **argv)
{
    startup_argc_ = argc;
    startup_argv_ = argv;
}

void AppStoreApp::activate(Screen screen)
{
    switch (screen) {
        case Screen::StartupSync: set_active_page(*this); break;
        case Screen::Home:
        case Screen::ShareCode:
        case Screen::Search: set_active_page(catalog_page_); break;
        case Screen::Registry:
        case Screen::RegistryEdit: set_active_page(settings_page_); break;
        case Screen::Detail:
        case Screen::Confirm:
        case Screen::Progress:
        case Screen::ErrorDialog:
        case Screen::Screenshots: set_active_page(detail_page_); break;
    }
}

void AppStoreApp::bind_page(appstore_ui::AppStoreUiPage &page)
{
    lv_obj_t *root = page.screen();
    lv_obj_set_size(root, kScreenWidth, kScreenHeight);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    event_adapter_.bind(root);
    lv_group_add_obj(page.input_group(), root);
    lv_group_focus_obj(root);
    page.add_top_bar_component(std::make_unique<AppStoreSyncTopBarComponent>());
}

void AppStoreApp::set_active_page(appstore_ui::AppStoreUiPage &page)
{
    if (active_page_ == &page) return;
    active_page_ = &page;
    page.activate();
}

} // namespace appstore_ui

void ui_init(int argc, char **argv)
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    cp0_zmq_log_init();
    if (auto *app = appstore_ui::AppStoreApp::current())
        app->lifecycle().initialize(argc, argv);
}

void ui_loop()
{
}

void ui_deinit()
{
    if (auto *app = appstore_ui::AppStoreApp::current())
        app->lifecycle().deinitialize();
}

bool ui_should_quit()
{
    auto *app = appstore_ui::AppStoreApp::current();
    return app && app->lifecycle().should_quit();
}

int run_appstore_app(int argc, char **argv)
{
    g_runtime.exit.reset();
    appstore_ui::AppStoreApp::set_arguments(argc, argv);
    Cp0LvglAppHooks hooks;
    hooks.should_quit = []() { return ui_should_quit(); };
    return cp0_lvgl_run_app<appstore_ui::AppStoreApp>(std::move(hooks));
}
