#pragma once

#include "appstore_session_state.hpp"
#include "exit_controller.hpp"
#include "package_job_state.hpp"

#include <cstdint>
#include <functional>

namespace appstore_ui {

class AppStoreLifecycle
{
public:
    struct Dependencies {
        std::function<void(int, char **)> configure_platform;
        std::function<void()> reset_status;
        std::function<void()> refresh_status;
        std::function<void()> initialize_sync;
        std::function<void()> render;
        std::function<void()> initialize_registry;
        std::function<void()> start_timers;
        std::function<void()> request_summary;
        std::function<void()> start_sync;
        std::function<void()> stop_timers;
        std::function<void()> destroy_overlay;
        std::function<void()> reset_low_battery;
        std::function<void()> stop_backend;
        std::function<bool()> sync_running;
        std::function<bool()> cancel_sync;
        std::function<bool()> cancel_prepare;
        std::function<int(uint64_t)> cancel_sudo;
        std::function<void(const std::string &, int)> finish_package;
        std::function<uint32_t()> tick_now;
    };

    AppStoreLifecycle(AppStoreSessionState &session, PackageJobState &package_job,
                      ExitController &exit, Dependencies dependencies);

    void initialize(int argc, char **argv);
    void deinitialize();
    bool should_quit();

private:
    static uint32_t elapsed(uint32_t now, uint32_t start)
    {
        return static_cast<uint32_t>(now - start);
    }

    AppStoreSessionState &session_;
    PackageJobState &package_job_;
    ExitController &exit_;
    Dependencies dependencies_;
    bool initialized_ = false;
};

} // namespace appstore_ui
