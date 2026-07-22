#include "sync_controller.hpp"

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

std::string sync_status_message(const std::string &output)
{
    return output.find("ERROR") == std::string::npos ? "Catalog synced" : "";
}

} // namespace appstore

int main()
{
    using namespace appstore_ui;
    AppStoreTaskCoordinator coordinator;
    std::mutex mutex;
    std::condition_variable worker_done;
    std::condition_variable release_worker;
    int finished = 0;
    bool release_first = false;
    appstore::DetachedWorkerLauncher workers({}, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++finished;
        worker_done.notify_all();
    });
    int backend_calls = 0;
    AppStoreTaskService tasks(coordinator, workers,
        [&](const std::vector<std::string> &args, int *rc) {
            assert(args == std::vector<std::string>({"--sync"}));
            const int call = ++backend_calls;
            if (call == 1) {
                std::unique_lock<std::mutex> lock(mutex);
                release_worker.wait(lock, [&]() { return release_first; });
            }
            *rc = call >= 4 ? 1 : 0;
            if (call == 3) return std::string("SYNC\t0\t1\t1\t26\tUsing cached catalog\n");
            if (call == 4) return std::string("SYNC\t0\t0\t1\t0\tRegistry unavailable\n");
            if (call == 5) return std::string("ERROR\tWi-Fi unavailable\n");
            return std::string("SYNC\t1\t0\t0\t26\tCatalog synced\n");
        });
    AppStoreSessionState session;
    int summaries = 0;
    SummaryPurpose last_summary_purpose = SummaryPurpose::Regular;
    int registry_refreshes = 0;
    int renders = 0;
    int cancels = 0;
    SyncController controller(
        session, tasks,
        {[]() {
             appstore::SyncStatus status;
             status.running = true;
             status.phase = "download";
             status.detail = "Downloading catalog";
             status.percent = 40;
             return status;
         },
         [&]() { ++cancels; return true; },
         []() { return false; },
         [&](SummaryPurpose purpose) {
             ++summaries;
             last_summary_purpose = purpose;
         },
         [&]() { ++registry_refreshes; },
         [&]() { ++renders; },
         []() { return true; }});

    auto wait_for = [&](int expected) {
        std::unique_lock<std::mutex> lock(mutex);
        worker_done.wait(lock, [&]() { return finished >= expected; });
    };

    controller.initialize_startup("https://registry.example");
    assert(session.screen == Screen::StartupSync && session.sync.startup_active);
    controller.start(false, 100);
    assert(controller.running());
    controller.poll(200, 200);
    assert(session.sync.status.phase == "download");
    assert(session.sync.animation_phase == 1);
    assert(renders >= 1);
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first = true;
    }
    release_worker.notify_all();
    wait_for(1);
    controller.poll(400, 200);
    assert(session.screen == Screen::StartupSync && session.sync.startup_active);
    assert(session.sync.status.phase == "catalog");
    assert(session.sync.status.detail == "Preparing app catalog...");
    assert(session.sync.status.percent == -1);
    assert(session.status.value() == "Catalog synced");
    assert(summaries == 1 && last_summary_purpose == SummaryPurpose::StartupCatalog &&
           registry_refreshes == 0);

    session.sync.startup_active = false;
    session.screen = Screen::Home;

    controller.start(true, 500);
    wait_for(2);
    controller.poll(600, 200);
    assert(summaries == 2 && registry_refreshes == 1);

    controller.initialize_startup("https://registry.example");
    controller.start(false, 700);
    wait_for(3);
    controller.poll(800, 200);
    assert(session.screen == Screen::StartupSync && session.sync.startup_active &&
           session.sync.startup_failed);
    assert(session.sync.failure_focus == 0);
    assert(summaries == 2);
    session.sync.startup_active = false;
    session.screen = Screen::Home;

    controller.initialize_startup("https://registry.example");
    controller.start(false, 900);
    wait_for(4);
    controller.poll(1000, 200);
    assert(session.screen == Screen::StartupSync);
    assert(session.sync.startup_failed && session.sync.startup_active);
    assert(session.error.title == "NETWORK FAILED");
    assert(summaries == 2);
    session.sync.startup_active = false;
    session.screen = Screen::Home;

    controller.initialize_startup("https://registry.example");
    controller.start(false, 1100);
    wait_for(5);
    controller.poll(1200, 200);
    assert(session.screen == Screen::StartupSync && session.sync.startup_active &&
           session.sync.startup_failed);
    assert(session.error.detail == "Wi-Fi unavailable");
    assert(summaries == 2);

    assert(controller.cancel() && cancels == 1);
    std::cout << "sync controller tests passed\n";
}
