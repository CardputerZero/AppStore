#pragma once

#include "appstore_session_state.hpp"
#include "package_job_state.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace appstore_ui {

class DetailActionController
{
public:
    using PlanStarter = std::function<void(const std::string &, const std::string &)>;
    using PlanRunning = std::function<bool()>;
    using ScreenshotStarter = std::function<bool(const std::string &)>;

    DetailActionController(AppStoreSessionState &session, PackageJobState &package_job,
                           PlanStarter start_plan, PlanRunning plan_running);
    void set_screenshot_starter(ScreenshotStarter starter)
    {
        start_screenshots_ = std::move(starter);
    }

    static std::vector<std::string> description_lines(const appstore::StoreApp &app);

    void install(appstore::StoreApp *app);
    void reinstall(appstore::StoreApp *app);
    void upgrade(appstore::StoreApp *app);
    void remove(appstore::StoreApp *app);
    void start_confirmation(const std::string &action);
    void cancel_confirmation();
    bool execute_confirmation(uint32_t now);
    bool apply_plan_result(const std::string &action, const std::string &app_id,
                           Screen origin_screen, int rc, const std::string &output);
    void cycle_screenshot(int delta, uint32_t now);
    bool open_screenshots(uint32_t now);
    void scroll_description(int delta);

private:
    appstore::StoreApp *selected_app();
    bool apply_plan(const std::string &output);

    AppStoreSessionState &session_;
    PackageJobState &package_job_;
    PlanStarter start_plan_;
    PlanRunning plan_running_;
    ScreenshotStarter start_screenshots_;
};

} // namespace appstore_ui
