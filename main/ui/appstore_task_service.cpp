#include "appstore_task_service.hpp"

#include <utility>

namespace appstore_ui {

TaskStartResult AppStoreTaskService::request_summary(appstore::SortRule rule,
                                                     SummaryLoader loader,
                                                     SummaryPurpose purpose)
{
    auto ticket = tasks_.summary().start_or_defer({rule, purpose});
    if (!ticket) return TaskStartResult::Deferred;
    if (workers_.start([this, ticket = *ticket, loader = std::move(loader)]() mutable {
            auto summary = loader(ticket.request.rule);
            tasks_.summary().finish(ticket.generation, std::move(summary));
        }) != 0) {
        tasks_.summary().fail_start(ticket->generation);
        return TaskStartResult::Failed;
    }
    return TaskStartResult::Started;
}

TaskStartResult AppStoreTaskService::request_registry_refresh(std::string fallback,
                                                              RegistryLoader loader)
{
    auto ticket = tasks_.registry_refresh().start_or_defer({std::move(fallback)});
    if (!ticket) return TaskStartResult::Deferred;
    if (workers_.start([this, ticket = *ticket, loader = std::move(loader)]() mutable {
            auto registries = loader(ticket.request.fallback);
            tasks_.registry_refresh().finish(ticket.generation, std::move(registries));
        }) != 0) {
        tasks_.registry_refresh().fail_start(ticket->generation);
        return TaskStartResult::Failed;
    }
    return TaskStartResult::Started;
}

TaskStartResult AppStoreTaskService::start_registry_operation(RegistryOpRequest request)
{
    auto ticket = tasks_.registry_operation().start(std::move(request));
    if (!ticket) return TaskStartResult::Busy;
    if (workers_.start([this, ticket = *ticket]() mutable {
            const auto arguments = AppStoreTaskCoordinator::registry_arguments(ticket.request);
            int rc = -1;
            std::string output = arguments.empty() ?
                "ERROR\tunknown registry operation\n" : backend_(arguments, &rc);
            tasks_.registry_operation().finish(ticket.generation, {std::move(output), rc});
        }) != 0) {
        tasks_.registry_operation().fail_start(ticket->generation);
        return TaskStartResult::Failed;
    }
    return TaskStartResult::Started;
}

TaskStartResult AppStoreTaskService::start_sync(bool refresh_registries_after)
{
    auto ticket = tasks_.sync().start({refresh_registries_after});
    if (!ticket) return TaskStartResult::Busy;
    if (workers_.start([this, ticket = *ticket]() mutable {
            int rc = -1;
            std::string output = backend_({"--sync"}, &rc);
            tasks_.sync().finish(ticket.generation, std::move(output));
        }) != 0) {
        tasks_.sync().fail_start(ticket->generation);
        return TaskStartResult::Failed;
    }
    return TaskStartResult::Started;
}

TaskStartResult AppStoreTaskService::start_plan(std::string action, std::string app_id,
                                                Screen origin)
{
    auto ticket = tasks_.plan().start({std::move(action), std::move(app_id), origin});
    if (!ticket) return TaskStartResult::Busy;
    if (workers_.start([this, ticket = *ticket]() mutable {
            int rc = -1;
            std::string output = backend_({"--plan", ticket.request.app_id}, &rc);
            tasks_.plan().finish(ticket.generation, {std::move(output), rc});
        }) != 0) {
        tasks_.plan().fail_start(ticket->generation);
        return TaskStartResult::Failed;
    }
    return TaskStartResult::Started;
}

TaskStartResult AppStoreTaskService::start_screenshots(std::string app_id)
{
    auto ticket = tasks_.screenshots().start({std::move(app_id)});
    if (!ticket) return TaskStartResult::Busy;
    if (workers_.start([this, ticket = *ticket]() mutable {
            int rc = -1;
            std::string output = backend_({"--fetch-screenshots", ticket.request.app_id}, &rc);
            tasks_.screenshots().finish(ticket.generation, {std::move(output), rc});
        }) != 0) {
        tasks_.screenshots().fail_start(ticket->generation);
        return TaskStartResult::Failed;
    }
    return TaskStartResult::Started;
}

} // namespace appstore_ui
