#include "appstore_input_controller.hpp"
#include "appstore_task_service.hpp"
#include "detached_worker_launcher.hpp"
#include "input_keys.h"

#include <cassert>
#include <iostream>

namespace appstore {

std::string one_line(std::string value, size_t max_len)
{
    if (value.size() > max_len) value.resize(max_len);
    return value;
}

std::string match_key(std::string value)
{
    for (char &ch : value)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
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
std::string sync_status_message(const std::string &) { return "Catalog synced"; }
void sort_apps(std::vector<StoreApp> &, SortRule) {}

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
    ExitController exit;
    ShareCodeState share_code;
    AppStoreTaskCoordinator coordinator;
    appstore::DetachedWorkerLauncher workers({}, {});
    AppStoreTaskService tasks(coordinator, workers,
        [](const std::vector<std::string> &, int *rc) { *rc = 0; return std::string{}; });
    CatalogController catalog(session, share_code, []() {});
    DetailActionController detail(session, package_job, [](const std::string &, const std::string &) {},
                                  []() { return false; });
    RegistryController registry(
        session, tasks, package_job,
        {[](const std::string &) { return appstore::RegistryData{}; },
         []() { return false; }, []() {}, []() {}, [](bool) {}, []() {}});
    SyncController sync(
        session, tasks,
        {[]() { return appstore::SyncStatus{}; }, []() { return true; },
         []() { return false; }, [](SummaryPurpose) {}, []() {}, []() {},
         []() { return true; }});

    int renders = 0;
    int quits = 0;
    int backs = 0;
    int repairs = 0;
    int registry_opens = 0;
    AppStoreInputController input(
        session, package_job, exit, share_code, catalog, detail, registry, sync,
        {[&]() { ++quits; },
         [&]() {
             ++backs;
             session.screen = session.sync.startup_active ? Screen::StartupSync : Screen::Home;
         },
         [&]() { ++registry_opens; session.screen = Screen::Registry; },
         [&]() { session.screen = Screen::RegistryEdit; },
         []() {}, [&]() { ++repairs; }, [&]() { ++renders; }});

    session.catalog.categories() = {"All"};
    appstore::StoreApp first;
    first.id = "one";
    first.name = "One";
    appstore::StoreApp second;
    second.id = "two";
    second.name = "Two";
    session.catalog.apps() = {first, second};
    session.catalog.visible() = {0, 1};
    session.screen = Screen::Home;
    input.handle({KEY_DOWN}, 100);
    assert(session.catalog.selected_index() == 1 && renders == 1);

    input.handle({KEY_5}, 110);
    assert(session.screen == Screen::ShareCode);
    input.handle({0, 0, 'A'}, 120);
    assert(share_code.input() == "A");
    input.handle({KEY_BACKSPACE}, 130);
    assert(share_code.input().empty());

    session.screen = Screen::Home;
    input.handle({KEY_6}, 140);
    assert(session.screen == Screen::Search);

    session.screen = Screen::Confirm;
    session.confirmation.focus() = 0;
    input.handle({KEY_TAB}, 150);
    assert(session.confirmation.focus() == 1);

    session.screen = Screen::RegistryEdit;
    session.registry.edit_focus() = 0;
    session.registry.name_input().clear();
    input.handle({0, 0, 'X'}, 160);
    assert(session.registry.name_input() == "X");

    session.screen = Screen::Detail;
    input.handle({KEY_ESC}, 200);
    input.handle({KEY_ESC, 0, 0, true}, 300);
    assert(backs == 1 && session.screen == Screen::Home);

    session.screen = Screen::StartupSync;
    session.sync.startup_failed = true;
    input.handle({KEY_ENTER}, 400);
    assert(quits == 1);
    session.screen = Screen::StartupSync;
    input.handle({KEY_RIGHT}, 410);
    assert(session.sync.failure_focus == 1);
    input.handle({KEY_ENTER}, 420);
    assert(registry_opens == 1 && session.screen == Screen::Registry);
    session.sync.startup_active = true;
    input.handle({KEY_B}, 430);
    assert(session.screen == Screen::StartupSync && session.sync.startup_failed);
    session.sync.startup_active = false;

    session.screen = Screen::ErrorDialog;
    session.error.repairable = true;
    input.handle({KEY_ENTER}, 500);
    assert(repairs == 1);
    input.handle({KEY_B}, 510);
    assert(backs == 3);

    std::cout << "appstore input controller tests passed\n";
}
