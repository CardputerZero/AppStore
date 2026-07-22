#include "appstore_task_service.hpp"

#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>

int main()
{
    appstore_ui::AppStoreTaskCoordinator tasks;
    std::mutex mutex;
    std::condition_variable done;
    int finished = 0;
    appstore::DetachedWorkerLauncher workers({}, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++finished;
        done.notify_one();
    });
    appstore_ui::AppStoreTaskService service(tasks, workers,
        [](const std::vector<std::string> &args, int *rc) {
            *rc = 0;
            if (args == std::vector<std::string>({"--sync"}))
                return std::string("SYNC\t1\n");
            if (args == std::vector<std::string>({"--fetch-screenshots", "app-id"}))
                return std::string("SCREENSHOT\tapp-id\t/tmp/shot.png\n");
            assert(args == std::vector<std::string>({"--plan", "app-id"}));
            return std::string("PLAN\n");
        });
    auto wait_for_worker = [&](int expected) {
        std::unique_lock<std::mutex> lock(mutex);
        done.wait(lock, [&]() { return finished >= expected; });
    };

    assert(service.request_summary(appstore::SortRule::New,
        [](appstore::SortRule rule) {
            appstore::SummaryData result;
            result.repo_status = rule == appstore::SortRule::New ? "new" : "wrong";
            return result;
        }, appstore_ui::SummaryPurpose::StartupCatalog) ==
        appstore_ui::TaskStartResult::Started);
    wait_for_worker(1);
    appstore_ui::SummaryRequest summary_request;
    appstore::SummaryData summary;
    assert(tasks.summary().take_result(summary_request, summary));
    assert(summary.repo_status == "new");
    assert(summary_request.purpose == appstore_ui::SummaryPurpose::StartupCatalog);

    assert(service.start_sync(true) == appstore_ui::TaskStartResult::Started);
    wait_for_worker(2);
    appstore_ui::SyncRequest sync_request;
    std::string sync_output;
    assert(tasks.sync().take_result(sync_request, sync_output));
    assert(sync_request.refresh_registries_after && sync_output == "SYNC\t1\n");

    assert(service.start_plan("install", "app-id", appstore_ui::Screen::Detail) ==
           appstore_ui::TaskStartResult::Started);
    wait_for_worker(3);
    appstore_ui::PlanRequest plan_request;
    appstore_ui::PlanResult plan_result;
    assert(tasks.plan().take_result(plan_request, plan_result));
    assert(plan_request.app_id == "app-id" && plan_result.rc == 0);

    assert(service.start_screenshots("app-id") == appstore_ui::TaskStartResult::Started);
    wait_for_worker(4);
    appstore_ui::ScreenshotRequest screenshot_request;
    appstore_ui::ScreenshotResult screenshot_result;
    assert(service.take_screenshots(screenshot_request, screenshot_result));
    assert(screenshot_request.app_id == "app-id" && screenshot_result.rc == 0);
    assert(screenshot_result.output.find("/tmp/shot.png") != std::string::npos);

    std::cout << "appstore task service tests passed\n";
}
