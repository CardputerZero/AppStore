#pragma once

#include "appstore_session_state.hpp"
#include "share_code_state.hpp"

#include <functional>
#include <string>

namespace appstore_ui {

class CatalogController
{
public:
    CatalogController(AppStoreSessionState &session, ShareCodeState &share_code,
                      std::function<void()> request_summary);

    appstore::StoreApp *ensure_selected();
    bool open_detail();
    bool select_app_index(int app_index);
    bool select_visible_app_by_id(const std::string &app_id);
    void select_adjacent_app(int delta);
    void select_adjacent_category(int delta);
    void cycle_sort_rule();
    void apply_summary(const appstore::SummaryData &summary);

    void open_share_code(uint32_t now);
    void submit_share_code();
    void open_search();
    void submit_search();
    void open_selected_search_result();

private:
    AppStoreSessionState &session_;
    ShareCodeState &share_code_;
    std::function<void()> request_summary_;
};

} // namespace appstore_ui
