#include "search_state.hpp"

#include "appstore_text.hpp"

namespace appstore_ui {

void SearchState::reset()
{
    input_.clear();
    message_ = "Search by app name, author, category, or code.";
    clear_results();
}

void SearchState::clear_results()
{
    results_.clear();
    selected_index_ = 0;
    results_active_ = false;
}

std::optional<int> SearchState::submit(const std::vector<appstore::StoreApp> &apps)
{
    const std::string query = appstore::match_key(input_);
    clear_results();
    if (query.empty()) {
        message_ = "Type a search term first.";
        return std::nullopt;
    }
    if (apps.empty()) {
        message_ = "Catalog is still loading.";
        return std::nullopt;
    }
    for (int index = 0; index < static_cast<int>(apps.size()); ++index) {
        const auto &app = apps[index];
        std::string categories;
        for (const auto &category : app.categories) categories += " " + category;
        const std::string haystack = appstore::match_key(
            app.name + " " + app.author + " " + app.category + categories +
            " " + app.share_code + " " + app.id);
        if (haystack.find(query) != std::string::npos) results_.push_back(index);
    }
    if (results_.empty()) {
        message_ = "No app found for: " + input_;
        return std::nullopt;
    }
    if (results_.size() == 1) {
        const int result = results_.front();
        clear_results();
        message_.clear();
        return result;
    }
    results_active_ = true;
    message_ = std::to_string(results_.size()) + " apps found. Select one.";
    return std::nullopt;
}

std::optional<int> SearchState::selected_result() const
{
    if (!results_active_ || results_.empty()) return std::nullopt;
    const int index = selected_index_ < 0 ? 0 :
        (selected_index_ >= static_cast<int>(results_.size()) ?
         static_cast<int>(results_.size()) - 1 : selected_index_);
    return results_[index];
}

void SearchState::move_selection(int delta)
{
    if (!results_active_ || results_.empty()) return;
    const int count = static_cast<int>(results_.size());
    selected_index_ = (selected_index_ + delta) % count;
    if (selected_index_ < 0) selected_index_ += count;
}

void SearchState::input_changed()
{
    clear_results();
    message_ = "Enter searches matching apps.";
}

} // namespace appstore_ui
