#pragma once

#include "job_output_buffer.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace appstore_ui {

enum class PackageJobPhase { Idle, Repair, Prepare, Sudo, Finalize };

struct PackageJobResultSnapshot {
    std::string output;
    int rc = -1;
    bool done = false;
};

class PackageJobState
{
public:
    void begin(std::string action_value, std::string app_id_value,
               std::string title_value, uint32_t now, bool force_overwrite_value = false);
    void reset();

    void clear_backend_result(bool clear_output);
    void complete_backend(std::string output, int result_code, bool replace_output);
    void append_output(const char *data, size_t size);
    void append_output(const std::string &data);
    PackageJobResultSnapshot result_snapshot() const;
    void set_completion(int result_code, bool is_done);
    bool parse_prepare_output(const std::string &output);
    std::vector<std::string> sudo_arguments(const std::string &backend_executable) const;

    bool running = false;
    bool pending_start = false;
    std::string action;
    std::string app_id;
    std::string title;
    std::string stage;
    std::string detail;
    std::string helper_action;
    std::string helper_value;
    std::string helper_desktop;
    std::string transaction_id;
    std::string pending_path;
    std::vector<std::string> helper_execs;
    bool helper_reinstall = false;
    bool force_overwrite = false;
    uint64_t sudo_request_id = 0;
    PackageJobPhase phase = PackageJobPhase::Idle;
    int progress = -1;
    uint32_t start_tick = 0;
    bool cancel_requested = false;

private:
    mutable std::mutex result_mutex_;
    appstore::JobOutputBuffer output_buffer_;
    int result_code_ = -1;
    bool done_ = false;
};

} // namespace appstore_ui
