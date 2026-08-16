#include "sudo_package_runner.hpp"

#include "lvgl/lvgl.h"

#include <vector>

namespace appstore_ui {

void SudoPackageRunner::output_callback(const char *data, size_t size, void *user)
{
    if (!data || size == 0 || !user) return;
    static_cast<SudoPackageRunner *>(user)->job_.append_output(data, size);
}

void SudoPackageRunner::complete_callback(cp0_sudo_result_t result, int exit_code,
                                          void *user)
{
    if (user) static_cast<SudoPackageRunner *>(user)->complete(result, exit_code);
}

void SudoPackageRunner::complete(cp0_sudo_result_t result, int exit_code)
{
    job_.sudo_request_id = 0;
    if (result == CP0_SUDO_RESULT_SUCCESS) {
        job_.phase = PackageJobPhase::Finalize;
        job_.pending_start = true;
        job_.clear_backend_result(false);
        job_.detail = job_.repairing ? "Finalizing package repair" :
            "Finalizing package state";
        job_.start_tick = lv_tick_get();
        return;
    }
    if (result == CP0_SUDO_RESULT_AUTH_FAILED)
        job_.append_output("ERROR\tSudo authentication failed after 3 attempts\n");
    else if (result == CP0_SUDO_RESULT_CANCELLED)
        job_.append_output("ERROR\tPackage operation cancelled\n");
    else if (result == CP0_SUDO_RESULT_TIMED_OUT)
        job_.append_output("ERROR\tPackage operation timed out\n");
    else if (job_.result_snapshot().output.find("ERROR\t") == std::string::npos)
        job_.append_output("ERROR\tPrivileged package command failed\t" +
                           std::to_string(exit_code) + "\n");
    job_.set_completion(exit_code == 0 ? 1 : exit_code, true);
}

bool SudoPackageRunner::start(const std::string &backend_executable)
{
    const std::vector<std::string> storage = job_.sudo_arguments(backend_executable);
    std::vector<const char *> argv;
    argv.reserve(storage.size() + 1);
    for (const std::string &arg : storage) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    const int rc = cp0_sudo_run_argv_async_ex(
        argv.data(), CP0_SUDO_CALLBACK_LVGL, output_callback, complete_callback, this,
        60 * 1000, 15 * 60 * 1000, &job_.sudo_request_id);
    if (rc != 0) {
        job_.append_output("ERROR\tUnable to start sudo request\n");
        job_.set_completion(rc, true);
        return false;
    }
    job_.phase = PackageJobPhase::Sudo;
    job_.detail = job_.repairing ? "Waiting to repair package manager" :
        "Waiting for sudo authentication";
    return true;
}

} // namespace appstore_ui
