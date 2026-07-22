#include "appstore_refresh_coordinator.hpp"

#include <utility>

namespace appstore_ui {

AppStoreRefreshCoordinator::AppStoreRefreshCoordinator(Dependencies dependencies)
    : dependencies_(std::move(dependencies))
{
}

void AppStoreRefreshCoordinator::render_if(bool changed)
{
    if (changed && dependencies_.render) dependencies_.render();
}

RefreshPollResult AppStoreRefreshCoordinator::poll(uint32_t now)
{
    RefreshPollResult result;
    if (dependencies_.tick_battery) dependencies_.tick_battery(now);
    if (dependencies_.poll_region_debounce)
        result.region_debounce = dependencies_.poll_region_debounce(now);
    render_if(result.region_debounce);
    if (dependencies_.poll_registry_refresh)
        result.registry_refresh = dependencies_.poll_registry_refresh();
    render_if(result.registry_refresh);
    if (dependencies_.poll_registry_operation)
        result.registry_operation = dependencies_.poll_registry_operation();
    render_if(result.registry_operation);
    if (dependencies_.poll_plan) result.plan = dependencies_.poll_plan();
    render_if(result.plan);
    if (dependencies_.poll_summary) result.summary = dependencies_.poll_summary();
    render_if(result.summary);
    if (dependencies_.status_expired)
        result.status_timeout = dependencies_.status_expired(now);
    render_if(result.status_timeout);
    if (dependencies_.top_status_due)
        result.top_status = dependencies_.top_status_due(now);
    render_if(result.top_status);
    if (dependencies_.hide_screenshot_overlay)
        result.screenshot_overlay = dependencies_.hide_screenshot_overlay(now);
    render_if(result.screenshot_overlay);
    return result;
}

} // namespace appstore_ui
