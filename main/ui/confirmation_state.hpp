#pragma once

#include <string>
#include <vector>

namespace appstore_ui {

class ConfirmationState
{
public:
    void begin(std::string action, std::string app_id);
    void reset();
    bool apply_plan(const std::string &output);

    std::string &action() { return action_; }
    const std::string &action() const { return action_; }
    std::string &app_id() { return app_id_; }
    const std::string &app_id() const { return app_id_; }
    std::vector<std::string> &lines() { return lines_; }
    const std::vector<std::string> &lines() const { return lines_; }
    int &focus() { return focus_; }
    int focus() const { return focus_; }
    bool &force_overwrite() { return force_overwrite_; }
    bool force_overwrite() const { return force_overwrite_; }

private:
    std::string action_;
    std::string app_id_;
    std::vector<std::string> lines_;
    int focus_ = 0;
    bool force_overwrite_ = false;
};

} // namespace appstore_ui
