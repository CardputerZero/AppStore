#pragma once

#include "detached_worker_launcher.hpp"
#include "package_job_state.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace appstore_ui {

enum class PackageWorkerStart { NotReady, Started, Failed };

class PackageJobService
{
public:
    using BackendCapture =
        std::function<std::string(const std::vector<std::string> &, int *)>;

    explicit PackageJobService(BackendCapture backend_capture)
        : backend_capture_(std::move(backend_capture))
    {
    }

    PackageJobState &state() { return state_; }
    const PackageJobState &state() const { return state_; }

    static std::vector<std::string> backend_arguments(const PackageJobState &state);
    PackageWorkerStart start_pending_worker(uint32_t now, uint32_t delay_ms,
                                            appstore::DetachedWorkerLauncher &workers);

private:
    PackageJobState state_;
    BackendCapture backend_capture_;
};

} // namespace appstore_ui
