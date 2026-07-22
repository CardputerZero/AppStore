#include "confirmation_state.hpp"

#include "appstore_protocol.hpp"

#include <sstream>

namespace appstore_ui {

void ConfirmationState::begin(std::string action, std::string app_id)
{
    action_ = std::move(action);
    app_id_ = std::move(app_id);
    lines_.clear();
    focus_ = 0;
    force_overwrite_ = false;
}

void ConfirmationState::reset()
{
    action_.clear();
    app_id_.clear();
    lines_.clear();
    focus_ = 0;
    force_overwrite_ = false;
}

bool ConfirmationState::apply_plan(const std::string &output)
{
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        const auto fields = appstore::split_tab(line);
        if (fields.size() < 8 || fields[0] != "PLAN") continue;
        lines_.clear();
        std::string verb = "Install ";
        if (action_ == "uninstall") verb = "Delete ";
        else if (action_ == "upgrade") verb = "Upgrade ";
        else if (action_ == "reinstall") verb = "Reinstall ";
        lines_.push_back(verb + fields[2]);
        lines_.push_back("Download/app size: " + fields[4]);
        lines_.push_back("Disk free: " + fields[5]);
        lines_.push_back("Dependencies: " + (fields[6].empty() ? "-" : fields[6]));
        lines_.push_back("Install checks: " + (fields[7].empty() ? "OK" : fields[7]));
        return true;
    }
    return false;
}

} // namespace appstore_ui
