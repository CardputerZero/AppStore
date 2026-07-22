#include "registry_controller.hpp"

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

std::string backend_error_message(const std::string &output) { return output; }

} // namespace appstore

int main()
{
    using namespace appstore_ui;
    AppStoreTaskCoordinator coordinator;
    std::mutex mutex;
    std::condition_variable done;
    int finished = 0;
    appstore::DetachedWorkerLauncher workers({}, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++finished;
        done.notify_all();
    });
    int backend_calls = 0;
    AppStoreTaskService tasks(coordinator, workers,
        [&](const std::vector<std::string> &args, int *rc) {
            ++backend_calls;
            *rc = 0;
            if (!args.empty() && args[0] == "--set-region")
                return std::string("REGION\tCN\tChina\thttps://cn.example/registry.json\tCN\n");
            return std::string("REGISTRY\tADDED\thttps://extra.example\tExtra\t7\n");
        });
    AppStoreSessionState session;
    PackageJobState package_job;
    int saved = 0;
    int summaries = 0;
    int syncs = 0;
    int stops = 0;
    RegistryController controller(
        session, tasks, package_job,
        {[](const std::string &fallback) {
             appstore::RegistryData data;
             data.region.code = "auto";
             data.region.label = "Auto";
             data.region.registry_url = fallback;
             data.entries.push_back({fallback, "Built in", "ok", "1", "", "", "default", true, true});
             return data;
         },
         []() { return false; },
         [&]() { ++saved; },
         [&]() { ++summaries; },
         [&](bool refresh) { if (refresh) ++syncs; },
         [&]() { ++stops; }});

    auto wait_for = [&](int expected) {
        std::unique_lock<std::mutex> lock(mutex);
        done.wait(lock, [&]() { return finished >= expected; });
    };

    controller.open();
    assert(session.screen == Screen::Registry && session.registry_loading);
    wait_for(1);
    assert(controller.poll_refresh());
    assert(!session.registry_loading && session.registry.entries().size() == 1);

    controller.delete_selected();
    assert(session.status.value() == "Use region setting");

    controller.select_region("CN", 100);
    assert(session.registry.region_code() == "CN");
    assert(session.registry.region_commit_pending());
    assert(stops == 1);
    assert(!controller.poll_region_debounce(2099, 2000));
    assert(controller.poll_region_debounce(2100, 2000));
    wait_for(2);
    assert(controller.poll_operation());
    assert(session.registry.region_label() == "China");
    assert(saved == 1 && summaries == 1 && syncs == 1 && backend_calls == 1);
    wait_for(3);
    assert(controller.poll_refresh());

    controller.open_add();
    session.registry.name_input() = "Extra";
    session.registry.input_url() = "https://extra.example/registry.json";
    controller.submit_editor();
    wait_for(4);
    assert(controller.poll_operation());
    assert(session.status.value() == "Registry added (7 apps)");
    assert(session.screen == Screen::Registry);
    assert(saved == 2 && syncs == 2 && backend_calls == 2);
    wait_for(5);
    assert(controller.poll_refresh());

    std::cout << "registry controller tests passed\n";
}
