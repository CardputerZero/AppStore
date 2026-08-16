#include "package_operation_controller.hpp"

#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>

namespace appstore {

std::string one_line(std::string value, size_t max_len)
{
    if (value.size() > max_len) value.resize(max_len);
    return value;
}

std::string upper_ascii(std::string value)
{
    for (char &ch : value) if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    return value;
}

std::string job_action_label(const std::string &action) { return action; }
std::string backend_error_message(const std::string &output)
{
    return output.find("ERROR") == std::string::npos ? std::string() : "Backend failed";
}

} // namespace appstore

int main()
{
    using namespace appstore_ui;
    AppStoreSessionState session;
    appstore::StoreApp app;
    app.id = "demo";
    app.name = "Demo";
    app.version = "2.0";
    session.catalog.apps().push_back(app);
    PackageJobService service(
        [](const std::vector<std::string> &args, int *rc) {
            assert(args == std::vector<std::string>({"--prepare-package", "install", "demo"}));
            *rc = 0;
            return std::string("PACKAGE_JOB\tinstall\t/tmp/pkg\t0\t\ttx-1\t/tmp/pending\n");
        });
    ExitController exit;
    std::mutex mutex;
    std::condition_variable done;
    int finished_workers = 0;
    appstore::DetachedWorkerLauncher workers({}, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++finished_workers;
        done.notify_all();
    });
    int sudo_starts = 0;
    int summaries = 0;
    int renders = 0;
    PackageOperationController controller(
        session, service, workers, exit,
        {[&]() {
             ++sudo_starts;
             service.state().phase = PackageJobPhase::Sudo;
             service.state().sudo_request_id = 77;
             service.state().detail = "Waiting for sudo";
             return true;
         },
         [](uint64_t) { return 0; },
         [&]() { ++summaries; },
         [&]() { ++renders; },
         []() { return false; }});

    service.state().begin("install", "demo", "Demo", 100);
    controller.poll(179, 80);
    assert(service.state().pending_start);
    controller.poll(180, 80);
    {
        std::unique_lock<std::mutex> lock(mutex);
        done.wait(lock, [&]() { return finished_workers == 1; });
    }
    controller.poll(200, 80);
    assert(sudo_starts == 1);
    assert(service.state().phase == PackageJobPhase::Sudo);
    assert(service.state().transaction_id == "tx-1");
    service.state().append_output(
        "PROGRESS\tverify\t0\t0\t-1\tVerifying package\n");
    controller.poll(205, 80);
    assert(service.state().stage == "verify");
    assert(service.state().detail == "Verifying package");

    service.state().phase = PackageJobPhase::Repair;
    service.state().repairing = true;
    service.state().pending_start = false;
    service.state().complete_backend("PACKAGE_REPAIR_READY\tdemo\tdemo-package\t1\n", 0, true);
    controller.poll(210, 80);
    assert(service.state().phase == PackageJobPhase::Prepare);
    assert(service.state().pending_start);
    assert(session.status.value() == "Starting system package repair...");
    service.state().pending_start = false;

    controller.finish(
        "PACKAGE_REPAIRED\tdemo\tdemo-package\trolled back\n"
        "PACKAGE_RESULT\trepair-rollback\tdemo\tdemo-package\t\n", 0);
    assert(!session.catalog.apps()[0].installed);
    assert(session.status.value() == "Package state repaired");
    assert(session.screen == Screen::Detail && summaries == 1);
    assert(!service.state().running);

    service.state().begin("upgrade", "demo", "Demo", 225);
    service.state().repairing = true;
    controller.finish(
        "PACKAGE_REPAIRED\tdemo\tdemo-package\trestored\n"
        "PACKAGE_RESULT\trepair-restored\tdemo\tdemo-package\t1.1\n", 0);
    assert(session.catalog.apps()[0].installed);
    assert(session.catalog.apps()[0].installed_version == "1.1");
    assert(session.status.value() == "Package state repaired");

    service.state().begin("install", "demo", "Demo", 250);
    controller.finish("PACKAGE_RESULT\tINSTALLED\tdemo\tDemo\t2.1\n", 0);
    assert(session.catalog.apps()[0].installed);
    assert(session.catalog.apps()[0].installed_version == "2.1");
    assert(session.status.value() == "Installed. Exit Store to test.");

    service.state().begin("uninstall", "demo", "Demo", 300);
    controller.finish("PACKAGE_RESULT\tUNINSTALLED\tdemo\tDemo\t\n", 0);
    assert(!session.catalog.apps()[0].installed);
    assert(session.status.value() == "Deleted");

    service.state().begin("upgrade", "demo", "Demo", 400);
    controller.finish("ERROR\tbroken\n", 1);
    assert(session.screen == Screen::ErrorDialog);
    assert(session.error.title == "UPGRADE FAILED");
    assert(session.error.detail == "Backend failed");
    assert(!session.error.repairable);

    service.state().begin("install", "demo", "Demo", 500);
    controller.finish(
        "ERROR\tpackage pending path is not the Store transaction file\n", 1);
    assert(session.error.repairable);
    assert(session.error.repair_action == "install");
    assert(session.error.repair_app_id == "demo");

    service.state().begin("install", "demo", "Demo", 550);
    controller.finish(
        "PENDING_CONFLICT\told-app\tupgrade\told-package\n"
        "ERROR\tanother package transaction is pending; finish or retry its original operation\n",
        1);
    assert(session.error.repairable);
    assert(session.error.repair_action == "upgrade");
    assert(session.error.repair_app_id == "old-app");
    assert(session.error.repair_title == "old-package");
    assert(session.error.detail.find("Enter Repair") != std::string::npos);

    service.state().begin("install", "demo", "Demo", 600);
    controller.finish(
        "dpkg: error: trying to overwrite '/shared/font', which is also in package applaunch\n"
        "ERROR\tpackage manager failed with exit code 1\n", 1);
    assert(session.screen == Screen::Confirm);
    assert(session.confirmation.action() == "install");
    assert(session.confirmation.force_overwrite());
    assert(session.confirmation.focus() == 1);
    assert(session.confirmation.lines()[0] == "FILE OWNERSHIP CONFLICT");
    assert(renders >= 2);

    std::cout << "package operation controller tests passed\n";
}
