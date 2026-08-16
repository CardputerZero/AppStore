#include "appstore_lifecycle.hpp"

#include "job_shutdown_flow.hpp"

#include <cerrno>
#include <utility>

namespace appstore_ui {

AppStoreLifecycle::AppStoreLifecycle(AppStoreSessionState &session,
                                     PackageJobState &package_job,
                                     ExitController &exit,
                                     Dependencies dependencies)
    : package_job_(package_job), exit_(exit),
      dependencies_(std::move(dependencies))
{
    (void)session;
}

void AppStoreLifecycle::initialize(int argc, char **argv)
{
    if (initialized_) return;
    initialized_ = true;
    if (dependencies_.reset_status) dependencies_.reset_status();
    if (dependencies_.configure_platform) dependencies_.configure_platform(argc, argv);
    if (dependencies_.refresh_status) dependencies_.refresh_status();
    if (dependencies_.initialize_sync) dependencies_.initialize_sync();
    if (dependencies_.render) dependencies_.render();
    if (dependencies_.initialize_registry) dependencies_.initialize_registry();
    if (dependencies_.render) dependencies_.render();
    if (dependencies_.start_timers) dependencies_.start_timers();
    if (dependencies_.request_summary) dependencies_.request_summary();
    if (dependencies_.start_sync) dependencies_.start_sync();
    if (dependencies_.render) dependencies_.render();
}

void AppStoreLifecycle::deinitialize()
{
    if (!initialized_) return;
    initialized_ = false;
    if (dependencies_.stop_timers) dependencies_.stop_timers();
    if (dependencies_.destroy_overlay) dependencies_.destroy_overlay();
    if (dependencies_.reset_low_battery) dependencies_.reset_low_battery();
    if (dependencies_.stop_backend) dependencies_.stop_backend();
}

bool AppStoreLifecycle::should_quit()
{
    const uint32_t now = dependencies_.tick_now ? dependencies_.tick_now() : 0;
    if (exit_.requested() && !exit_.background_cancel_sent()) {
        if (!dependencies_.sync_running || !dependencies_.sync_running() ||
            (elapsed(now, exit_.background_retry_tick()) >= 250 &&
             dependencies_.cancel_sync && dependencies_.cancel_sync())) {
            exit_.background_cancel_sent() = true;
        } else if (elapsed(now, exit_.background_retry_tick()) >= 250) {
            exit_.background_retry_tick() = now;
        }
    }

    ShutdownJobPhase phase = ShutdownJobPhase::Idle;
    if (package_job_.phase == PackageJobPhase::Prepare ||
        package_job_.phase == PackageJobPhase::Repair) phase = ShutdownJobPhase::Prepare;
    else if (package_job_.phase == PackageJobPhase::Sudo) phase = ShutdownJobPhase::Sudo;
    else if (package_job_.phase == PackageJobPhase::Finalize) phase = ShutdownJobPhase::Finalize;
    const ShutdownAction action = shutdown_action(
        exit_.requested(), package_job_.running, package_job_.pending_start,
        exit_.job_cancel_sent(), phase, package_job_.sudo_request_id != 0);
    if (action == ShutdownAction::Ready) return exit_.worker_count() == 0;
    if (action == ShutdownAction::KeepRunning) return false;
    if (elapsed(now, exit_.job_retry_tick()) < 250) return false;
    exit_.job_retry_tick() = now;
    package_job_.cancel_requested = true;

    if (action == ShutdownAction::CancelPendingPrepare) {
        exit_.job_cancel_sent() = true;
        if (dependencies_.finish_package)
            dependencies_.finish_package("ERROR\tPackage operation cancelled\n", 1);
    } else if (action == ShutdownAction::CancelPrepare) {
        if (dependencies_.cancel_prepare && dependencies_.cancel_prepare())
            exit_.job_cancel_sent() = true;
    } else if (action == ShutdownAction::CancelSudo) {
        const int rc = dependencies_.cancel_sudo ?
            dependencies_.cancel_sudo(package_job_.sudo_request_id) : -1;
        if (rc == 0 || rc == -ENOENT) exit_.job_cancel_sent() = true;
    }
    return false;
}

} // namespace appstore_ui
