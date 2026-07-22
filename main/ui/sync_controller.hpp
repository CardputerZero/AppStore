#pragma once

#include "appstore_session_state.hpp"
#include "appstore_task_service.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace appstore_ui {

class SyncController
{
public:
    struct Dependencies {
        std::function<appstore::SyncStatus()> load_status;
        std::function<bool()> cancel;
        std::function<bool()> exit_requested;
        std::function<void(SummaryPurpose)> request_summary;
        std::function<void()> request_registry_refresh;
        std::function<void()> render;
        std::function<bool()> allow_animation_render;
    };

    SyncController(AppStoreSessionState &session, AppStoreTaskService &tasks,
                   Dependencies dependencies);

    void initialize_startup(const std::string &registry_url);
    void start(bool refresh_registries_after, uint32_t now);
    void poll(uint32_t now, uint32_t animation_interval_ms);
    bool running() { return tasks_.sync_running(); }
    bool cancel() { return dependencies_.cancel && dependencies_.cancel(); }

private:
    static bool startup_failed(const std::string &output, std::string *message);
    void apply_output(const std::string &output, bool refresh_registries_after);
    void set_start_failure();
    void continue_after_startup_failure(const std::string &detail);

    AppStoreSessionState &session_;
    AppStoreTaskService &tasks_;
    Dependencies dependencies_;
};

} // namespace appstore_ui
