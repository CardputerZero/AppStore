#include "appstore_refresh_coordinator.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using namespace appstore_ui;
    std::vector<std::string> calls;
    int renders = 0;
    AppStoreRefreshCoordinator coordinator(
        {[&](uint32_t now) { assert(now == 1234); calls.push_back("battery"); },
         [&](uint32_t now) { assert(now == 1234); calls.push_back("region"); return true; },
         [&]() { calls.push_back("registry-refresh"); return false; },
         [&]() { calls.push_back("registry-operation"); return true; },
         [&]() { calls.push_back("plan"); return false; },
         [&]() { calls.push_back("summary"); return true; },
         [&](uint32_t now) { assert(now == 1234); calls.push_back("status"); return true; },
         [&](uint32_t now) { assert(now == 1234); calls.push_back("top-status"); return false; },
         [&](uint32_t now) { assert(now == 1234); calls.push_back("screenshot"); return true; },
         [&]() { ++renders; }});

    const RefreshPollResult result = coordinator.poll(1234);
    assert(result.region_debounce);
    assert(!result.registry_refresh);
    assert(result.registry_operation);
    assert(!result.plan);
    assert(result.summary && result.status_timeout && result.screenshot_overlay);
    assert(!result.top_status && result.any());
    assert(renders == 5);
    assert(calls == std::vector<std::string>({
        "battery", "region", "registry-refresh", "registry-operation", "plan",
        "summary", "status", "top-status", "screenshot"}));

    AppStoreRefreshCoordinator idle({});
    const RefreshPollResult idle_result = idle.poll(9);
    assert(!idle_result.any());
    std::cout << "appstore refresh coordinator tests passed\n";
}
