#include "package_job_service.hpp"

namespace appstore_ui {

std::vector<std::string> PackageJobService::backend_arguments(const PackageJobState &state)
{
    if (state.phase == PackageJobPhase::Repair)
        return {"--repair-package-transaction", state.app_id};
    if (state.phase == PackageJobPhase::Prepare)
        return {"--prepare-package", state.action, state.app_id};
    if (state.phase == PackageJobPhase::Finalize)
        return {"--finalize-package", state.action, state.app_id, state.transaction_id};
    return {};
}

PackageWorkerStart PackageJobService::start_pending_worker(
    uint32_t now, uint32_t delay_ms, appstore::DetachedWorkerLauncher &workers)
{
    if (!state_.pending_start || static_cast<uint32_t>(now - state_.start_tick) < delay_ms)
        return PackageWorkerStart::NotReady;

    state_.pending_start = false;
    state_.clear_backend_result(state_.phase == PackageJobPhase::Prepare);
    const PackageJobPhase phase = state_.phase;
    const auto arguments = backend_arguments(state_);
    if (arguments.empty() || workers.start(
            [this, phase, arguments]() mutable {
                int rc = -1;
                std::string output = backend_capture_(arguments, &rc);
                state_.complete_backend(std::move(output), rc, phase == PackageJobPhase::Prepare);
            }) != 0) {
        state_.append_output("ERROR\tUnable to start package worker\n");
        state_.set_completion(1, true);
        return PackageWorkerStart::Failed;
    }

    state_.start_tick = now;
    state_.detail = phase == PackageJobPhase::Repair ? "Repairing package transaction" :
        (phase == PackageJobPhase::Prepare ? "Preparing package files" :
         "Updating package records");
    return PackageWorkerStart::Started;
}

} // namespace appstore_ui
