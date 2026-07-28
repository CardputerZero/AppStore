#include "catalog_state.hpp"

#include <algorithm>
#include <cstdio>

namespace appstore_ui {

const std::string &CatalogState::current_category_name() const
{
    static const std::string all = "All";
    if (categories_.empty() || category_index_ < 0 ||
        category_index_ >= static_cast<int>(categories_.size())) return all;
    return categories_[category_index_];
}

bool CatalogState::select_category_by_name(const std::string &name)
{
    for (int index = 0; index < static_cast<int>(categories_.size()); ++index) {
        if (categories_[index] == name) {
            category_index_ = index;
            return true;
        }
    }
    return false;
}

void CatalogState::select_default_category()
{
    if (!select_category_by_name("All") && !categories_.empty()) category_index_ = 0;
    std::fprintf(stderr, "[Store UI] select_default_category index=%d name=%s cats=%zu\n",
                 category_index_, current_category_name().c_str(), categories_.size());
}

void CatalogState::rebuild_visible()
{
    visible_.clear();
    const std::string &category = current_category_name();
    int recommended_count = 0;
    int exact_category_count = 0;
    for (int index = 0; index < static_cast<int>(apps_.size()); ++index) {
        const auto &app = apps_[index];
        const bool category_match = app.category == category ||
            std::find(app.categories.begin(), app.categories.end(), category) !=
                app.categories.end();
        if (app.recommended) ++recommended_count;
        if (category_match) ++exact_category_count;
        const bool show = category == "All" ||
            (category == "Recommended" && app.recommended) || category_match;
        if (show) visible_.push_back(index);
    }
    if (selected_index_ >= static_cast<int>(visible_.size()))
        selected_index_ = static_cast<int>(visible_.size()) - 1;
    if (selected_index_ < 0) selected_index_ = 0;
    std::fprintf(stderr,
                 "[Store UI] rebuild_visible category=%s category_index=%d apps=%zu visible=%zu recommended=%d exact_category=%d selected=%d\n",
                 category.c_str(), category_index_, apps_.size(), visible_.size(),
                 recommended_count, exact_category_count, selected_index_);
}

appstore::StoreApp *CatalogState::selected_app()
{
    if (visible_.empty() || selected_index_ < 0 ||
        selected_index_ >= static_cast<int>(visible_.size())) return nullptr;
    return &apps_[visible_[selected_index_]];
}

const appstore::StoreApp *CatalogState::selected_app() const
{
    if (visible_.empty() || selected_index_ < 0 ||
        selected_index_ >= static_cast<int>(visible_.size())) return nullptr;
    return &apps_[visible_[selected_index_]];
}

} // namespace appstore_ui
