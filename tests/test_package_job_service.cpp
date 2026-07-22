#include "package_job_service.hpp"

#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>

int main()
{
    appstore_ui::PackageJobService service(
        [](const std::vector<std::string> &args, int *rc) {
            assert(args[0] == "--prepare-package");
            *rc = 0;
            return std::string("PACKAGE_JOB\tinstall\t/tmp/pkg\t0\t\ttx\t/tmp/pending\n");
        });
    service.state().begin("install", "app-id", "Example", 100);
    std::mutex mutex;
    std::condition_variable done;
    bool finished = false;
    appstore::DetachedWorkerLauncher workers({}, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
        done.notify_one();
    });
    assert(service.start_pending_worker(179, 80, workers) ==
           appstore_ui::PackageWorkerStart::NotReady);
    assert(appstore_ui::PackageJobService::backend_arguments(service.state()) ==
           std::vector<std::string>({"--prepare-package", "install", "app-id"}));
    service.state().phase = appstore_ui::PackageJobPhase::Repair;
    assert(appstore_ui::PackageJobService::backend_arguments(service.state()) ==
           std::vector<std::string>({"--repair-package-transaction", "app-id"}));
    service.state().phase = appstore_ui::PackageJobPhase::Prepare;
    assert(service.start_pending_worker(180, 80, workers) ==
           appstore_ui::PackageWorkerStart::Started);
    std::unique_lock<std::mutex> lock(mutex);
    done.wait(lock, [&]() { return finished; });
    const auto result = service.state().result_snapshot();
    assert(result.done && result.rc == 0);
    assert(result.output.find("PACKAGE_JOB") != std::string::npos);

    service.state().phase = appstore_ui::PackageJobPhase::Finalize;
    service.state().transaction_id = "tx";
    assert(appstore_ui::PackageJobService::backend_arguments(service.state()) ==
           std::vector<std::string>({"--finalize-package", "install", "app-id", "tx"}));
    std::cout << "package job service tests passed\n";
}
