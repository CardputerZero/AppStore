#pragma once

#include <cstdint>
#include <functional>

namespace appstore_ui {

struct RefreshPollResult {
    bool region_debounce = false;
    bool registry_refresh = false;
    bool registry_operation = false;
    bool plan = false;
    bool summary = false;
    bool status_timeout = false;
    bool top_status = false;
    bool screenshot_overlay = false;

    bool any() const
    {
        return region_debounce || registry_refresh || registry_operation || plan ||
               summary || status_timeout || top_status || screenshot_overlay;
    }
};

class AppStoreRefreshCoordinator
{
public:
    struct Dependencies {
        std::function<void(uint32_t)> tick_battery;
        std::function<bool(uint32_t)> poll_region_debounce;
        std::function<bool()> poll_registry_refresh;
        std::function<bool()> poll_registry_operation;
        std::function<bool()> poll_plan;
        std::function<bool()> poll_summary;
        std::function<bool(uint32_t)> status_expired;
        std::function<bool(uint32_t)> top_status_due;
        std::function<bool(uint32_t)> hide_screenshot_overlay;
        std::function<void()> render;
    };

    explicit AppStoreRefreshCoordinator(Dependencies dependencies);
    RefreshPollResult poll(uint32_t now);

private:
    void render_if(bool changed);

    Dependencies dependencies_;
};

} // namespace appstore_ui
