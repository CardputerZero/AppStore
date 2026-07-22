#pragma once

#include "appstore_task_coordinator.hpp"
#include "detached_worker_launcher.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace appstore_ui {

enum class TaskStartResult { Started, Deferred, Busy, Failed };

class AppStoreTaskService
{
public:
    using SummaryLoader = std::function<appstore::SummaryData(appstore::SortRule)>;
    using RegistryLoader = std::function<appstore::RegistryData(const std::string &)>;
    using BackendCapture =
        std::function<std::string(const std::vector<std::string> &, int *)>;

    AppStoreTaskService(AppStoreTaskCoordinator &tasks,
                        appstore::DetachedWorkerLauncher &workers,
                        BackendCapture backend)
        : tasks_(tasks), workers_(workers), backend_(std::move(backend))
    {
    }

    TaskStartResult request_summary(appstore::SortRule rule, SummaryLoader loader,
                                    SummaryPurpose purpose = SummaryPurpose::Regular);
    TaskStartResult request_registry_refresh(std::string fallback, RegistryLoader loader);
    TaskStartResult start_registry_operation(RegistryOpRequest request);
    TaskStartResult start_sync(bool refresh_registries_after);
    TaskStartResult start_plan(std::string action, std::string app_id, Screen origin);
    TaskStartResult start_screenshots(std::string app_id);

    bool take_summary(SummaryRequest &request, appstore::SummaryData &result,
                      std::optional<SummaryRequest> *deferred = nullptr)
    {
        return tasks_.summary().take_result(request, result, deferred);
    }
    bool take_registry_refresh(RegistryRefreshRequest &request, appstore::RegistryData &result,
                               std::optional<RegistryRefreshRequest> *deferred = nullptr)
    {
        return tasks_.registry_refresh().take_result(request, result, deferred);
    }
    bool take_registry_operation(RegistryOpRequest &request, RegistryOpResult &result)
    {
        return tasks_.registry_operation().take_result(request, result);
    }
    bool take_sync(SyncRequest &request, std::string &result)
    {
        return tasks_.sync().take_result(request, result);
    }
    bool take_plan(PlanRequest &request, PlanResult &result)
    {
        return tasks_.plan().take_result(request, result);
    }
    bool take_screenshots(ScreenshotRequest &request, ScreenshotResult &result)
    {
        return tasks_.screenshots().take_result(request, result);
    }

    bool summary_running() { return tasks_.summary().running(); }
    bool registry_refresh_running() { return tasks_.registry_refresh().running(); }
    bool registry_operation_running() { return tasks_.registry_operation().running(); }
    bool sync_running() { return tasks_.sync().running(); }
    bool plan_running() { return tasks_.plan().running(); }
    bool screenshots_running() { return tasks_.screenshots().running(); }
    std::optional<RegistryOpRequest> active_registry_operation()
    {
        return tasks_.registry_operation().active_request();
    }
    void cancel_catalog_work() { tasks_.cancel_catalog_work(); }
    void cancel_registry_operation() { tasks_.registry_operation().cancel(); }

private:
    AppStoreTaskCoordinator &tasks_;
    appstore::DetachedWorkerLauncher &workers_;
    BackendCapture backend_;
};

} // namespace appstore_ui
