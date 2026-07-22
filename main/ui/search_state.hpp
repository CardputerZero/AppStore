#pragma once

#include "appstore_client.hpp"

#include <optional>
#include <string>
#include <vector>

namespace appstore_ui {

class SearchState
{
public:
    void reset();
    void clear_results();
    std::optional<int> submit(const std::vector<appstore::StoreApp> &apps);
    std::optional<int> selected_result() const;
    void move_selection(int delta);
    void input_changed();

    std::string &input() { return input_; }
    const std::string &input() const { return input_; }
    std::string &message() { return message_; }
    const std::string &message() const { return message_; }
    const std::vector<int> &results() const { return results_; }
    std::vector<int> &results() { return results_; }
    int selected_index() const { return selected_index_; }
    int &selected_index() { return selected_index_; }
    bool results_active() const { return results_active_; }

private:
    std::string input_;
    std::string message_ = "Search by app name, author, category, or code.";
    std::vector<int> results_;
    int selected_index_ = 0;
    bool results_active_ = false;
};

} // namespace appstore_ui
