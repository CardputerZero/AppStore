#pragma once

#include "package_job_state.hpp"
#include "cp0_lvgl_app.h"

#include <string>

namespace appstore_ui {

class SudoPackageRunner
{
public:
    explicit SudoPackageRunner(PackageJobState &job) : job_(job) {}

    bool start(const std::string &backend_executable);

private:
    static void output_callback(const char *data, size_t size, void *user);
    static void complete_callback(cp0_sudo_result_t result, int exit_code, void *user);
    void complete(cp0_sudo_result_t result, int exit_code);

    PackageJobState &job_;
};

} // namespace appstore_ui
