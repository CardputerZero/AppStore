#include "sync_controller.hpp"

#include "startup_network_flow.hpp"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace appstore_ui {

using namespace appstore;

SyncController::SyncController(AppStoreSessionState &session, AppStoreTaskService &tasks,
                               Dependencies dependencies)
    : session_(session), tasks_(tasks), dependencies_(std::move(dependencies))
{
}

void SyncController::initialize_startup(const std::string &registry_url)
{
    session_.screen = Screen::StartupSync;
    session_.sync.startup_active = true;
    session_.sync.startup_failed = false;
    session_.sync.failure_focus = 0;
    session_.sync.status = {};
    session_.sync.status.detail = "Checking network connection...";
    session_.sync.status.phase = "network";
    session_.sync.status.url = registry_url;
}

void SyncController::start(bool refresh_registries_after, uint32_t now)
{
    if (dependencies_.exit_requested && dependencies_.exit_requested()) return;
    const TaskStartResult started = tasks_.start_sync(refresh_registries_after);
    if (started == TaskStartResult::Busy) {
        session_.status.value() = "Sync already running";
        return;
    }
    session_.sync.animation_phase = -1;
    session_.sync.visible_until_tick = now;
    session_.status.value() = session_.sync.startup_active
        ? "Checking network..." : "Syncing catalog...";
    if (started != TaskStartResult::Failed) return;
    session_.sync.visible_until_tick = 0;
    session_.status.value() = "Unable to start sync";
    if (session_.sync.startup_active) set_start_failure();
}

void SyncController::set_start_failure()
{
    session_.sync.startup_failed = true;
    session_.sync.failure_focus = 0;
    session_.sync.status.running = false;
    session_.sync.status.percent = -1;
    session_.sync.status.phase = "network";
    session_.sync.status.detail = "Unable to start network check";
    session_.status.value() = "Network unavailable - press R to retry";
    session_.error.title = "NETWORK FAILED";
    session_.error.message = "No registry is reachable.";
    session_.error.detail = "Exit Store or open settings to change registry addresses.";
    if (dependencies_.render) dependencies_.render();
}

bool SyncController::startup_failed(const std::string &output, std::string *message)
{
    std::string fallback;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        const auto fields = split_tab(line);
        if (fields.empty()) continue;
        if (fields[0] == "ERROR") {
            if (message) *message = fields.size() >= 2 && !fields[1].empty()
                ? fields[1] : "Network connection failed";
            return true;
        }
        if (fields[0] != "SYNC" || fields.size() < 6) continue;
        const int ok = std::atoi(fields[1].c_str());
        const int failed = std::atoi(fields[3].c_str());
        if (!fields[5].empty()) fallback = fields[5];
        if (ok <= 0) {
            if (message) *message = fields[5].empty()
                ? (failed > 0 ? "No registry could be loaded" : "Network connection failed")
                : fields[5];
            return true;
        }
    }
    if (message) *message = fallback.empty() ? "Network connection failed" : fallback;
    return false;
}

void SyncController::continue_after_startup_failure(const std::string &detail)
{
    session_.sync.startup_failed = true;
    session_.sync.failure_focus = 0;
    session_.sync.status.running = false;
    session_.sync.status.percent = -1;
    session_.sync.status.phase = "network";
    session_.sync.status.detail = detail.empty() ? "Network unavailable" : detail;
    session_.error.title = "NETWORK FAILED";
    session_.error.message = "No registry is reachable.";
    session_.error.detail = detail.empty()
        ? "Exit Store or open settings to change registry addresses."
        : detail;
    session_.status.value().clear();
    session_.screen = Screen::StartupSync;
}

void SyncController::apply_output(const std::string &output, bool refresh_registries_after)
{
    const std::string message = sync_status_message(output);
    if (!message.empty()) session_.status.value() = one_line(message, 54);
    else if (output.find("SYNC\t0") != std::string::npos)
        session_.status.value() = "No apps loaded";
    else session_.status.value().clear();
    if (dependencies_.request_summary)
        dependencies_.request_summary(session_.sync.startup_active
            ? SummaryPurpose::StartupCatalog : SummaryPurpose::Regular);
    if (refresh_registries_after && dependencies_.request_registry_refresh)
        dependencies_.request_registry_refresh();
}

void SyncController::poll(uint32_t now, uint32_t animation_interval_ms)
{
    SyncRequest request;
    std::string output;
    const bool done = tasks_.take_sync(request, output);
    if (tasks_.sync_running()) {
        if (session_.sync.startup_active || session_.screen == Screen::StartupSync) {
            SyncStatus status = dependencies_.load_status ? dependencies_.load_status() : SyncStatus{};
            const bool changed = status.running != session_.sync.status.running ||
                status.cancel_requested != session_.sync.status.cancel_requested ||
                status.url != session_.sync.status.url || status.detail != session_.sync.status.detail ||
                status.percent != session_.sync.status.percent || status.phase != session_.sync.status.phase;
            session_.sync.status = std::move(status);
            if (changed && session_.screen == Screen::StartupSync && dependencies_.render)
                dependencies_.render();
        }
        const int phase = static_cast<int>((now / animation_interval_ms) % 4);
        if (phase != session_.sync.animation_phase) {
            session_.sync.animation_phase = phase;
            if ((!dependencies_.allow_animation_render ||
                 dependencies_.allow_animation_render()) && dependencies_.render)
                dependencies_.render();
        }
        return;
    }
    if (!done) return;
    session_.sync.animation_phase = -1;
    if (session_.sync.startup_active) {
        std::string error;
        const bool failed = startup_failed(output, &error);
        if (startup_network_completion(failed) == StartupNetworkCompletion::STAY_FAILED) {
            continue_after_startup_failure(error);
            if (dependencies_.render) dependencies_.render();
            return;
        }
        session_.sync.startup_failed = false;
        session_.sync.failure_focus = 0;
        session_.error.clear();
        session_.sync.status.running = false;
        // Catalog parsing is a separate asynchronous task without byte-level
        // progress. Keep the progress bar moving instead of leaving it at 100%.
        session_.sync.status.percent = -1;
        session_.sync.status.phase = "catalog";
        session_.sync.status.detail = "Preparing app catalog...";
        apply_output(output, request.refresh_registries_after);
    } else {
        apply_output(output, request.refresh_registries_after);
    }
    if (dependencies_.render) dependencies_.render();
}

} // namespace appstore_ui
