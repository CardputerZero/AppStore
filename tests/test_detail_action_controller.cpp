#include "detail_action_controller.hpp"

#include <cassert>
#include <iostream>

namespace appstore {

std::string one_line(std::string value, size_t max_len)
{
    if (value.size() > max_len) value.resize(max_len);
    return value;
}

std::vector<std::string> wrap_display_text(std::string text, int)
{
    return {std::move(text)};
}

bool has_blocking_missing(const std::string &missing) { return !missing.empty(); }
std::string missing_install_message(const std::string &) { return "Missing dependencies"; }
bool can_install_app(const StoreApp &app) { return app.installable; }
bool can_reinstall_app(const StoreApp &app) { return app.installable && app.installed; }
bool can_upgrade_app(const StoreApp &app)
{
    return app.installable && app.installed && app.version != app.installed_version;
}
std::string backend_error_message(const std::string &output) { return output; }

} // namespace appstore

std::vector<std::string> detail_screenshot_paths(const std::string &,
                                                  const appstore::StoreApp &)
{
    return {};
}

int main()
{
    using namespace appstore_ui;
    AppStoreSessionState session;
    PackageJobState package_job;
    std::string started_action;
    std::string started_app;
    bool plan_running = false;
    DetailActionController controller(
        session, package_job,
        [&](const std::string &action, const std::string &app_id) {
            started_action = action;
            started_app = app_id;
        },
        [&]() { return plan_running; });
    std::string screenshot_app;
    controller.set_screenshot_starter([&](const std::string &app_id) {
        screenshot_app = app_id;
        return true;
    });

    controller.install(nullptr);
    assert(session.status.value() == "No selected app");

    appstore::StoreApp app;
    app.id = "demo";
    app.name = "Demo";
    app.version = "2.0";
    app.installed_version = "1.0";
    session.catalog.categories() = {"All"};
    session.catalog.apps() = {app};
    session.catalog.rebuild_visible();

    controller.install(session.catalog.selected_app());
    assert(session.status.value() == "Only approved apps can install");

    session.catalog.selected_app()->installable = true;
    controller.install(session.catalog.selected_app());
    assert(started_action == "install" && started_app == "demo");
    assert(session.confirmation.action() == "install");

    session.screen = Screen::Detail;
    const std::string plan = "PLAN\tdemo\tDemo\t2.0\t12 MB\t80 MB\tlibfoo\t\n";
    assert(controller.apply_plan_result("install", "demo", Screen::Detail, 0, plan));
    assert(session.screen == Screen::Confirm);
    assert(!session.confirmation.lines().empty());

    assert(controller.execute_confirmation(1234));
    assert(package_job.running && package_job.pending_start);
    assert(package_job.action == "install" && package_job.app_id == "demo");
    assert(session.screen == Screen::Progress);

    package_job.reset();
    session.catalog.selected_app()->installed = true;
    session.screen = Screen::Detail;
    session.free_space = "42 MB";
    controller.remove(session.catalog.selected_app());
    assert(session.screen == Screen::Confirm);
    assert(session.confirmation.lines()[0] == "Delete Demo");
    controller.cancel_confirmation();
    assert(session.screen == Screen::Detail && session.confirmation.action().empty());

    session.screen = Screen::Home;
    assert(!controller.apply_plan_result("upgrade", "demo", Screen::Detail, 0, plan));
    assert(session.status.value() == "Install plan expired");

    plan_running = true;
    session.screen = Screen::Detail;
    controller.start_confirmation("upgrade");
    assert(session.status.value() == "Plan check already running");

    assert(controller.open_screenshots(900));
    assert(session.screen == Screen::Screenshots);
    assert(screenshot_app == "demo");
    assert(session.detail_media.loading());

    std::cout << "detail action controller tests passed\n";
}
