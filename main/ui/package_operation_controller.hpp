#pragma once

#include "appstore_session_state.hpp"
#include "exit_controller.hpp"
#include "package_job_service.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace appstore_ui {

class PackageOperationController
{
public:
    struct Dependencies {
        std::function<bool()> start_sudo;
        std::function<int(uint64_t)> cancel_sudo;
        std::function<void()> request_summary;
        std::function<void()> render;
        std::function<bool()> exit_requested;
    };

    PackageOperationController(AppStoreSessionState &session, PackageJobService &service,
                               appstore::DetachedWorkerLauncher &workers,
                               ExitController &exit, Dependencies dependencies);

    void poll(uint32_t now, uint32_t worker_start_delay_ms);
    void finish(const std::string &output, int result_code);

private:
    void poll_backend(uint32_t now);
    void parse_progress(const std::string &output);
    void update_local_app_state(bool ok, const std::string &output);

    AppStoreSessionState &session_;
    PackageJobService &service_;
    PackageJobState &job_;
    appstore::DetachedWorkerLauncher &workers_;
    ExitController &exit_;
    Dependencies dependencies_;
};

} // namespace appstore_ui
