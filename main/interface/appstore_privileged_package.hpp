#pragma once

#include <string>
#include <vector>

namespace appstore {

struct PrivilegedPackageRequest {
    std::string action;
    std::string value;
    bool reinstall = false;
    bool force_overwrite = false;
    std::string desktop;
    std::string transaction_id;
    std::string pending_path;
    std::vector<std::string> executable_candidates;
};

// The CLI protocol is private to the backend adapter. UI code submits a typed request.
inline std::vector<std::string> privileged_package_argv(
    const std::string &backend_executable, const PrivilegedPackageRequest &request)
{
    std::vector<std::string> arguments = {
        backend_executable, "--package-helper", request.action,
        "--package-value", request.value};
    if (request.reinstall) arguments.push_back("--package-reinstall");
    if (request.force_overwrite) arguments.push_back("--package-force-overwrite");
    if (!request.desktop.empty()) {
        arguments.push_back("--package-desktop");
        arguments.push_back(request.desktop);
    }
    arguments.push_back("--package-transaction");
    arguments.push_back(request.transaction_id);
    arguments.push_back("--package-pending-path");
    arguments.push_back(request.pending_path);
    for (const auto &value : request.executable_candidates) {
        arguments.push_back("--package-exec");
        arguments.push_back(value);
    }
    return arguments;
}

} // namespace appstore
