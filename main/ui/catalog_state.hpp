#pragma once

#include "appstore_client.hpp"

#include <string>
#include <vector>

namespace appstore_ui {

class CatalogState
{
public:
    const std::string &current_category_name() const;
    bool select_category_by_name(const std::string &name);
    void select_default_category();
    void rebuild_visible();

    appstore::StoreApp *selected_app();
    const appstore::StoreApp *selected_app() const;

    std::vector<std::string> &categories() { return categories_; }
    const std::vector<std::string> &categories() const { return categories_; }
    std::vector<appstore::StoreApp> &apps() { return apps_; }
    const std::vector<appstore::StoreApp> &apps() const { return apps_; }
    std::vector<int> &visible() { return visible_; }
    const std::vector<int> &visible() const { return visible_; }
    int &category_index() { return category_index_; }
    int category_index() const { return category_index_; }
    int &selected_index() { return selected_index_; }
    int selected_index() const { return selected_index_; }
    bool &default_category_applied() { return default_category_applied_; }
    bool default_category_applied() const { return default_category_applied_; }
    appstore::SortRule &sort_rule() { return sort_rule_; }
    appstore::SortRule sort_rule() const { return sort_rule_; }

private:
    std::vector<std::string> categories_{"Recommended", "All"};
    std::vector<appstore::StoreApp> apps_;
    std::vector<int> visible_;
    int category_index_ = 0;
    int selected_index_ = 0;
    bool default_category_applied_ = false;
    appstore::SortRule sort_rule_ = appstore::SortRule::Default;
};

} // namespace appstore_ui
