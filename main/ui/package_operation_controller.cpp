#include "package_operation_controller.hpp"

#include <cerrno>
#include <sstream>
#include <utility>

namespace appstore_ui {

using namespace appstore;

PackageOperationController::PackageOperationController(
    AppStoreSessionState &session, PackageJobService &service,
    DetachedWorkerLauncher &workers, ExitController &exit, Dependencies dependencies)
    : session_(session), service_(service), job_(service.state()), workers_(workers),
      exit_(exit), dependencies_(std::move(dependencies))
{
}

void PackageOperationController::parse_progress(const std::string &output)
{
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        const auto fields = split_tab(line);
        if (fields.size() >= 6 && fields[0] == "PROGRESS") {
            job_.stage = fields[1];
            job_.progress = std::atoi(fields[4].c_str());
            job_.detail = fields[5];
        }
    }
}

void PackageOperationController::update_local_app_state(bool ok,
                                                        const std::string &output)
{
    if (!ok || job_.app_id.empty()) return;
    for (StoreApp &app : session_.catalog.apps()) {
        if (app.id != job_.app_id) continue;
        std::string actual_version;
        std::string result_action;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            const auto fields = split_tab(line);
            if (fields.size() >= 5 && fields[0] == "PACKAGE_RESULT") {
                result_action = fields[1];
                actual_version = fields[4];
                break;
            }
        }
        const bool removed = result_action == "uninstall" ||
            result_action == "repair-rollback" || result_action == "UNINSTALLED";
        const bool installed = result_action == "install" || result_action == "reinstall" ||
            result_action == "upgrade" || result_action == "INSTALLED" ||
            result_action == "UPGRADED" || result_action == "repair-restored";
        if (removed) {
            app.installed = false;
            app.installed_version.clear();
        } else if (installed) {
            app.installed = true;
            app.installed_version = actual_version.empty() ? app.version : actual_version;
        }
        return;
    }
}

void PackageOperationController::finish(const std::string &output, int result_code)
{
    bool has_result = false;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const auto fields = split_tab(line);
        if (!fields.empty() && fields[0] == "PACKAGE_RESULT") {
            has_result = true;
            break;
        }
    }
    const bool ok = result_code == 0 && has_result;
    const std::string action = job_.action;
    const std::string app_id = job_.app_id;
    const std::string title = job_.title.empty() ? "Selected app" : job_.title;
    const bool was_repairing = job_.repairing;
    update_local_app_state(ok, output);
    const bool overwrite_conflict = !ok && action != "uninstall" &&
        output.find("trying to overwrite") != std::string::npos &&
        output.find("also in package") != std::string::npos;
    if (overwrite_conflict) {
        session_.confirmation.begin(action, app_id);
        session_.confirmation.force_overwrite() = true;
        session_.confirmation.focus() = 1;
        session_.confirmation.lines() = {
            "FILE OWNERSHIP CONFLICT",
            "A file belongs to another package.",
            "Force overwrite may affect that package.",
        };
        session_.error.clear();
        session_.status.value().clear();
        job_.reset();
        if (!dependencies_.exit_requested || !dependencies_.exit_requested())
            session_.screen = Screen::Confirm;
        return;
    }
    if (!ok) {
        std::string pending_app_id;
        std::string pending_action;
        std::string pending_package;
        bool repair_required = false;
        std::istringstream pending_lines(output);
        while (std::getline(pending_lines, line)) {
            const auto fields = split_tab(line);
            if (fields.size() >= 4 && fields[0] == "PENDING_CONFLICT") {
                pending_app_id = fields[1];
                pending_action = fields[2];
                pending_package = fields[3];
                break;
            }
            if (fields.size() >= 4 && fields[0] == "REPAIR_REQUIRED") {
                pending_app_id = fields[1];
                pending_action = fields[2];
                pending_package = fields[3];
                repair_required = true;
            }
        }
        const bool pending_conflict = !pending_app_id.empty() && !pending_action.empty();
        session_.error.title = was_repairing ? "REPAIR FAILED" :
            upper_ascii(job_action_label(action)) + " FAILED";
        session_.error.message = one_line(title, 36);
        session_.error.detail = backend_error_message(output);
        if (session_.error.detail.empty()) session_.error.detail = "Package operation failed.";
        session_.error.repairable = pending_conflict || repair_required || was_repairing ||
            output.find("package pending path is not the Store transaction file") != std::string::npos;
        if (pending_conflict && !repair_required) {
            session_.error.message = "Interrupted package transaction";
            session_.error.detail = "An earlier " + pending_action + " for " +
                (pending_package.empty() ? pending_app_id : pending_package) +
                " is incomplete. Enter Repair to recover it.";
        } else if (repair_required || was_repairing) {
            session_.error.message = was_repairing ? "Package recovery incomplete" :
                "Package transaction needs repair";
            session_.error.detail = was_repairing ?
                "Recovery stopped safely. Enter Repair to resume the bounded recovery." :
                "The package manager changed state. Enter Repair to finish or roll it back.";
        }
        if (session_.error.repairable) {
            session_.error.repair_action = pending_conflict ? pending_action : action;
            session_.error.repair_app_id = pending_conflict ? pending_app_id : job_.app_id;
            session_.error.repair_title = pending_conflict ?
                (pending_package.empty() ? pending_app_id : pending_package) : title;
        }
        session_.status.value().clear();
    } else if (output.find("PACKAGE_REPAIRED\t") != std::string::npos) {
        session_.status.value() = "Package state repaired";
    } else if (action == "uninstall" || output.find("UNINSTALLED") != std::string::npos) {
        session_.status.value() = "Deleted";
    } else if (action == "upgrade" || output.find("UPGRADED") != std::string::npos) {
        session_.status.value() = "Upgraded. Exit Store to test.";
    } else if (output.find("INSTALLED") != std::string::npos) {
        session_.status.value() = "Installed. Exit Store to test.";
    } else {
        session_.status.value() = "Done";
    }
    job_.reset();
    if (!dependencies_.exit_requested || !dependencies_.exit_requested()) {
        if (dependencies_.request_summary) dependencies_.request_summary();
        session_.screen = ok ? Screen::Detail : Screen::ErrorDialog;
    }
}

void PackageOperationController::poll_backend(uint32_t now)
{
    if (!job_.running || job_.pending_start) return;
    const uint32_t elapsed = static_cast<uint32_t>(now - job_.start_tick) / 1000;
    auto result = job_.result_snapshot();
    std::string output = result.output;
    int rc = result.rc;
    parse_progress(output);
    std::string detail = job_.detail.empty() ? job_action_label(job_.action) : job_.detail;
    if (job_.progress >= 0) detail += " " + std::to_string(job_.progress) + "%";
    if (job_.cancel_requested) session_.status.value() = "Cancelling package operation...";
    else session_.status.value() = detail + " " + one_line(job_.title, 16) + " " +
                                   std::to_string(elapsed) + "s";
    if (!result.done) return;
    if (job_.phase == PackageJobPhase::Repair && rc == 0 &&
        output.find("PACKAGE_REPAIR_READY\t") != std::string::npos) {
        job_.phase = PackageJobPhase::Prepare;
        job_.pending_start = true;
        job_.clear_backend_result(true);
        job_.detail = "Starting system package repair";
        job_.start_tick = now;
        session_.status.value() = "Starting system package repair...";
        return;
    }
    if (job_.phase == PackageJobPhase::Prepare && rc == 0) {
        if (job_.parse_prepare_output(output)) {
            job_.clear_backend_result(false);
            if (dependencies_.start_sudo && dependencies_.start_sudo() &&
                job_.cancel_requested && job_.sudo_request_id != 0) {
                exit_.job_cancel_sent() = false;
                const int cancel_rc = dependencies_.cancel_sudo
                    ? dependencies_.cancel_sudo(job_.sudo_request_id) : -1;
                if (cancel_rc == 0 || cancel_rc == -ENOENT) exit_.job_cancel_sent() = true;
                session_.status.value() = "Cancelling package operation...";
            }
            return;
        }
        if (job_.cancel_requested) {
            job_.append_output("ERROR\tPackage operation cancelled\n");
            job_.set_completion(1, true);
            finish(job_.result_snapshot().output, 1);
            return;
        }
        job_.append_output("ERROR\tPackage preparation returned no helper command\n");
        job_.set_completion(1, true);
        output = job_.result_snapshot().output;
        rc = 1;
    }
    finish(output, rc);
}

void PackageOperationController::poll(uint32_t now, uint32_t worker_start_delay_ms)
{
    if (!job_.running && !job_.pending_start) return;
    const PackageWorkerStart started = service_.start_pending_worker(
        now, worker_start_delay_ms, workers_);
    if (started == PackageWorkerStart::Failed) {
        finish(job_.result_snapshot().output, 1);
        if (dependencies_.render) dependencies_.render();
        return;
    }
    if (started == PackageWorkerStart::Started) {
        session_.status.value() = job_.repairing ?
            "Repairing " + one_line(job_.title, 18) + "... 0s" :
            job_action_label(job_.action) + " " + one_line(job_.title, 18) + "... 0s";
    }
    poll_backend(now);
    if ((!dependencies_.exit_requested || !dependencies_.exit_requested()) && dependencies_.render)
        dependencies_.render();
}

} // namespace appstore_ui
