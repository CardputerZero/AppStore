#include "package_job_state.hpp"

#include "appstore_protocol.hpp"
#include "appstore_privileged_package.hpp"

#include <sstream>
#include <utility>

namespace appstore_ui {

void PackageJobState::begin(std::string action_value, std::string app_id_value,
                            std::string title_value, uint32_t now,
                            bool force_overwrite_value)
{
    reset();
    running = true;
    pending_start = true;
    action = std::move(action_value);
    app_id = std::move(app_id_value);
    title = std::move(title_value);
    force_overwrite = force_overwrite_value;
    detail = "Preparing package worker";
    phase = PackageJobPhase::Prepare;
    start_tick = now;
}

void PackageJobState::reset()
{
    running = false;
    pending_start = false;
    action.clear();
    app_id.clear();
    title.clear();
    stage.clear();
    detail.clear();
    helper_action.clear();
    helper_value.clear();
    helper_desktop.clear();
    transaction_id.clear();
    pending_path.clear();
    helper_execs.clear();
    helper_reinstall = false;
    force_overwrite = false;
    repairing = false;
    sudo_request_id = 0;
    phase = PackageJobPhase::Idle;
    progress = -1;
    start_tick = 0;
    cancel_requested = false;
    clear_backend_result(true);
}

void PackageJobState::clear_backend_result(bool clear_output)
{
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (clear_output) output_buffer_.clear();
    result_code_ = -1;
    done_ = false;
}

void PackageJobState::complete_backend(std::string output, int result_code, bool replace_output)
{
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (replace_output) output_buffer_.clear();
    output_buffer_.append(output.data(), output.size());
    result_code_ = result_code;
    done_ = true;
}

void PackageJobState::append_output(const char *data, size_t size)
{
    std::lock_guard<std::mutex> lock(result_mutex_);
    output_buffer_.append(data, size);
}

void PackageJobState::append_output(const std::string &data)
{
    append_output(data.data(), data.size());
}

PackageJobResultSnapshot PackageJobState::result_snapshot() const
{
    std::lock_guard<std::mutex> lock(result_mutex_);
    return {output_buffer_.snapshot(), result_code_, done_};
}

void PackageJobState::set_completion(int result_code, bool is_done)
{
    std::lock_guard<std::mutex> lock(result_mutex_);
    result_code_ = result_code;
    done_ = is_done;
}

bool PackageJobState::parse_prepare_output(const std::string &output)
{
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("PACKAGE_JOB\t", 0) != 0) continue;
        const auto values = appstore::split_tab(line);
        if (values.size() < 7) return false;
        helper_action = values[1];
        helper_value = values[2];
        helper_reinstall = values[3] == "1";
        helper_desktop = values[4];
        transaction_id = values[5];
        pending_path = values[6];
        helper_execs.clear();
        for (size_t index = 7; index < values.size(); ++index)
            if (!values[index].empty()) helper_execs.push_back(values[index]);
        return !helper_action.empty() && !helper_value.empty() && !transaction_id.empty();
    }
    return false;
}

std::vector<std::string> PackageJobState::sudo_arguments(
    const std::string &backend_executable) const
{
    return appstore::privileged_package_argv(
        backend_executable,
        {helper_action, helper_value, helper_reinstall, force_overwrite, helper_desktop,
         transaction_id, pending_path, helper_execs});
}

} // namespace appstore_ui
