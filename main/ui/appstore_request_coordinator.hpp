#pragma once

#include "appstore_session_state.hpp"
#include "appstore_task_service.hpp"
#include "catalog_controller.hpp"
#include "detail_action_controller.hpp"

#include <functional>
#include <string>

namespace appstore_ui {

class AppStoreRequestCoordinator
{
public:
    struct Dependencies {
        AppStoreTaskService::SummaryLoader load_summary;
        std::function<bool()> exit_requested;
        std::function<void(const std::string &)> log;
    };

    AppStoreRequestCoordinator(AppStoreSessionState &session, AppStoreTaskService &tasks,
                               CatalogController &catalog, DetailActionController &detail,
                               Dependencies dependencies);

    void request_summary(SummaryPurpose purpose = SummaryPurpose::Regular);
    bool poll_summary();
    void start_plan(const std::string &action, const std::string &app_id);
    bool poll_plan();
    bool plan_running() { return tasks_.plan_running(); }

private:
    void log(const std::string &message) const;

    AppStoreSessionState &session_;
    AppStoreTaskService &tasks_;
    CatalogController &catalog_;
    DetailActionController &detail_;
    Dependencies dependencies_;
};

} // namespace appstore_ui
