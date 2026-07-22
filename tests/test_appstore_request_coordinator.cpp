#include "appstore_request_coordinator.hpp"
#include "detached_worker_launcher.hpp"

#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>

namespace appstore {

std::string one_line(std::string value, size_t max_len)
{
    if (value.size() > max_len) value.resize(max_len);
    return value;
}
std::string match_key(std::string value) { return value; }
void sort_apps(std::vector<StoreApp> &, SortRule) {}
std::vector<std::string> wrap_display_text(std::string text, int) { return {std::move(text)}; }
bool has_blocking_missing(const std::string &missing) { return !missing.empty(); }
std::string missing_install_message(const std::string &) { return "Missing dependencies"; }
bool can_install_app(const StoreApp &app) { return app.installable; }
bool can_reinstall_app(const StoreApp &app) { return app.installable && app.installed; }
bool can_upgrade_app(const StoreApp &app) { return app.installable && app.installed; }
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
    std::mutex mutex;
    std::condition_variable completed;
    std::condition_variable release_loader;
    int workers_done = 0;
    int loader_calls = 0;
    bool release_first_loader = false;
    appstore::DetachedWorkerLauncher workers({}, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++workers_done;
        completed.notify_all();
    });
    AppStoreTaskCoordinator task_slots;
    AppStoreTaskService tasks(task_slots, workers,
        [](const std::vector<std::string> &, int *rc) {
            *rc = 0;
            return std::string("PLAN\tdemo\tDemo\t2.0\t12 MB\t80 MB\t\t\n");
        });
    AppStoreSessionState session;
    ShareCodeState share_code;
    PackageJobState package_job;
    CatalogController catalog(session, share_code, []() {});
    DetailActionController detail(session, package_job,
                                  [](const std::string &, const std::string &) {},
                                  [&]() { return tasks.plan_running(); });
    AppStoreRequestCoordinator requests(
        session, tasks, catalog, detail,
        {[&](appstore::SortRule) {
             int call = 0;
             {
                 std::unique_lock<std::mutex> lock(mutex);
                 call = ++loader_calls;
                 if (call == 1)
                     release_loader.wait(lock, [&]() { return release_first_loader; });
             }
             appstore::SummaryData summary;
             summary.saw_meta = true;
             summary.categories = {"All"};
             appstore::StoreApp app;
             app.id = call == 1 ? "stale" : "demo";
             app.name = "Demo";
             app.version = "2.0";
             app.installable = true;
             summary.apps.push_back(app);
             return summary;
         },
         []() { return false; }, [](const std::string &) {}});

    auto wait_for = [&](int expected) {
        std::unique_lock<std::mutex> lock(mutex);
        completed.wait(lock, [&]() { return workers_done >= expected; });
    };

    session.screen = Screen::StartupSync;
    session.sync.startup_active = true;
    requests.request_summary(SummaryPurpose::Regular);
    requests.request_summary(SummaryPurpose::StartupCatalog);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first_loader = true;
    }
    release_loader.notify_all();
    wait_for(1);
    assert(requests.poll_summary());
    assert(session.screen == Screen::StartupSync && session.sync.startup_active);
    assert(session.catalog.apps().size() == 1);
    assert(session.catalog.apps()[0].id == "stale");

    wait_for(2);
    assert(requests.poll_summary());
    assert(session.screen == Screen::Home && !session.sync.startup_active);
    assert(session.catalog.apps().size() == 1);
    assert(session.catalog.apps()[0].id == "demo");

    session.screen = Screen::Detail;
    requests.start_plan("install", "demo");
    assert(session.status.value() == "Checking install plan...");
    wait_for(3);
    assert(requests.poll_plan());
    assert(session.screen == Screen::Confirm);
    assert(session.confirmation.action() == "install");

    std::cout << "appstore request coordinator tests passed\n";
}
