#pragma once

#include "appstore_session_state.hpp"
#include "appstore_task_service.hpp"
#include "package_job_state.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace appstore_ui {

class RegistryController
{
public:
    struct Dependencies {
        AppStoreTaskService::RegistryLoader load_registries;
        std::function<bool()> exit_requested;
        std::function<void()> save_config;
        std::function<void()> request_summary;
        std::function<void(bool)> sync_catalog;
        std::function<void()> stop_backend;
    };

    RegistryController(AppStoreSessionState &session, AppStoreTaskService &tasks,
                       PackageJobState &package_job, Dependencies dependencies);

    void open();
    void open_add();
    void request_refresh();
    bool poll_refresh();
    bool poll_operation();
    bool operation_running() { return tasks_.registry_operation_running(); }
    bool operation_available();
    void cancel_online_work();

    void select_region(const std::string &region, uint32_t now);
    std::string adjacent_region(int delta) const;
    bool poll_region_debounce(uint32_t now, uint32_t delay_ms);

    void submit_editor();
    void toggle_selected();
    void delete_selected();
    void edit_selected();

private:
    bool refresh_running();
    bool start_operation(const RegistryOpRequest &request, const std::string &status);
    void apply_region_output(const std::string &output);
    static std::string count_message(const std::string &output, bool editing);

    AppStoreSessionState &session_;
    AppStoreTaskService &tasks_;
    PackageJobState &package_job_;
    Dependencies dependencies_;
};

} // namespace appstore_ui
