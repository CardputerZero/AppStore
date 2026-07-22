#include "appstore_request_coordinator.hpp"

#include <utility>

namespace appstore_ui {

AppStoreRequestCoordinator::AppStoreRequestCoordinator(
    AppStoreSessionState &session, AppStoreTaskService &tasks,
    CatalogController &catalog, DetailActionController &detail,
    Dependencies dependencies)
    : session_(session), tasks_(tasks), catalog_(catalog), detail_(detail),
      dependencies_(std::move(dependencies))
{
}

void AppStoreRequestCoordinator::log(const std::string &message) const
{
    if (dependencies_.log) dependencies_.log(message);
}

void AppStoreRequestCoordinator::request_summary(SummaryPurpose purpose)
{
    if (dependencies_.exit_requested && dependencies_.exit_requested()) return;
    const appstore::SortRule rule = session_.catalog.sort_rule();
    const TaskStartResult started = tasks_.request_summary(rule, dependencies_.load_summary,
                                                           purpose);
    if (started == TaskStartResult::Deferred) {
        log("summary refresh deferred: already running");
        return;
    }
    if (started == TaskStartResult::Failed) {
        session_.status.value() = "Unable to refresh app list";
        if (purpose == SummaryPurpose::StartupCatalog) {
            session_.sync.startup_active = false;
            if (session_.screen == Screen::StartupSync) session_.screen = Screen::Home;
        }
        log("summary refresh failed: worker start");
        return;
    }
    log("summary refresh started rule=" + std::to_string(static_cast<int>(rule)));
}

bool AppStoreRequestCoordinator::poll_summary()
{
    SummaryRequest request;
    appstore::SummaryData summary;
    std::optional<SummaryRequest> deferred;
    if (!tasks_.take_summary(request, summary, &deferred)) return false;
    log("summary refresh completed apps=" + std::to_string(summary.apps.size()) +
        " categories=" + std::to_string(summary.categories.size()));
    catalog_.apply_summary(summary);
    if (request.purpose == SummaryPurpose::StartupCatalog) {
        session_.sync.startup_active = false;
        if (session_.screen == Screen::StartupSync) session_.screen = Screen::Home;
    }
    if (deferred) request_summary(deferred->purpose);
    return true;
}

void AppStoreRequestCoordinator::start_plan(const std::string &action,
                                            const std::string &app_id)
{
    if (dependencies_.exit_requested && dependencies_.exit_requested()) return;
    const TaskStartResult started = tasks_.start_plan(action, app_id, session_.screen);
    if (started == TaskStartResult::Busy) {
        session_.status.value() = "Plan check already running";
        return;
    }
    session_.status.value() = "Checking install plan...";
    if (started == TaskStartResult::Failed)
        session_.status.value() = "Unable to check install plan";
}

bool AppStoreRequestCoordinator::poll_plan()
{
    PlanRequest request;
    PlanResult result;
    if (!tasks_.take_plan(request, result)) return false;
    detail_.apply_plan_result(request.action, request.app_id, request.origin_screen,
                              result.rc, result.output);
    return true;
}

} // namespace appstore_ui
