#include "catalog_controller.hpp"

#include "appstore_text.hpp"

#include <algorithm>
#include <utility>

namespace appstore_ui {

using namespace appstore;

CatalogController::CatalogController(AppStoreSessionState &session,
                                     ShareCodeState &share_code,
                                     std::function<void()> request_summary)
    : session_(session), share_code_(share_code),
      request_summary_(std::move(request_summary))
{
}

StoreApp *CatalogController::ensure_selected()
{
    StoreApp *app = session_.catalog.selected_app();
    if (app) return app;
    if (!session_.catalog.apps().empty()) {
        session_.catalog.select_default_category();
        session_.catalog.rebuild_visible();
        session_.catalog.selected_index() = 0;
        app = session_.catalog.selected_app();
        if (app) return app;
    }
    if (request_summary_) request_summary_();
    return nullptr;
}

bool CatalogController::open_detail()
{
    if (ensure_selected()) {
        session_.screen = Screen::Detail;
        return true;
    }
    session_.status.value() = session_.catalog.apps().empty()
        ? "Catalog is still loading" : "No app selected";
    session_.screen = Screen::Home;
    return false;
}

bool CatalogController::select_visible_app_by_id(const std::string &app_id)
{
    if (app_id.empty()) return false;
    for (int index = 0; index < static_cast<int>(session_.catalog.visible().size()); ++index) {
        if (session_.catalog.apps()[session_.catalog.visible()[index]].id == app_id) {
            session_.catalog.selected_index() = index;
            return true;
        }
    }
    return false;
}

bool CatalogController::select_app_index(int app_index)
{
    if (app_index < 0 || app_index >= static_cast<int>(session_.catalog.apps().size()))
        return false;
    session_.catalog.select_category_by_name("All");
    session_.catalog.rebuild_visible();
    for (int index = 0; index < static_cast<int>(session_.catalog.visible().size()); ++index) {
        if (session_.catalog.visible()[index] == app_index) {
            session_.catalog.selected_index() = index;
            return true;
        }
    }
    return false;
}

void CatalogController::select_adjacent_app(int delta)
{
    if (session_.catalog.visible().empty()) return;
    const int count = static_cast<int>(session_.catalog.visible().size());
    int next = (session_.catalog.selected_index() + delta) % count;
    if (next < 0) next += count;
    session_.catalog.selected_index() = next;
}

void CatalogController::select_adjacent_category(int delta)
{
    if (session_.catalog.categories().empty()) return;
    const int count = static_cast<int>(session_.catalog.categories().size());
    int next = (session_.catalog.category_index() + delta) % count;
    if (next < 0) next += count;
    session_.catalog.category_index() = next;
    session_.catalog.rebuild_visible();
}

void CatalogController::cycle_sort_rule()
{
    StoreApp *app = session_.catalog.selected_app();
    const std::string selected_id = app ? app->id : "";
    switch (session_.catalog.sort_rule()) {
        case SortRule::Default: session_.catalog.sort_rule() = SortRule::New; break;
        case SortRule::New: session_.catalog.sort_rule() = SortRule::Old; break;
        case SortRule::Old: session_.catalog.sort_rule() = SortRule::AtoZ; break;
        case SortRule::AtoZ: session_.catalog.sort_rule() = SortRule::ZtoA; break;
        case SortRule::ZtoA: session_.catalog.sort_rule() = SortRule::Default; break;
    }
    sort_apps(session_.catalog.apps(), session_.catalog.sort_rule());
    session_.catalog.rebuild_visible();
    if (!select_visible_app_by_id(selected_id)) session_.catalog.selected_index() = 0;
    session_.status.value().clear();
}

void CatalogController::apply_summary(const SummaryData &summary)
{
    StoreApp *previous = session_.catalog.selected_app();
    const std::string previous_id = previous ? previous->id : "";
    const std::string previous_category = session_.catalog.current_category_name();
    if (!summary.categories.empty()) session_.catalog.categories() = summary.categories;
    if (summary.saw_meta) {
        session_.repo_status = summary.repo_status;
        session_.free_space = summary.free_space;
        session_.root_path = summary.root_path;
        session_.catalog.apps() = summary.apps;
        sort_apps(session_.catalog.apps(), session_.catalog.sort_rule());
    }
    if (!session_.catalog.default_category_applied()) {
        session_.catalog.select_default_category();
        session_.catalog.default_category_applied() = true;
        session_.catalog.selected_index() = 0;
    } else if (!previous_category.empty() &&
               session_.catalog.select_category_by_name(previous_category)) {
    } else if (session_.catalog.category_index() >=
               static_cast<int>(session_.catalog.categories().size())) {
        session_.catalog.select_default_category();
    }
    session_.catalog.rebuild_visible();
    select_visible_app_by_id(previous_id);
    if (!summary.warning.empty()) {
        session_.status.value() = one_line(summary.warning, 54);
    } else if (session_.status.value().rfind("Registry offline", 0) == 0 ||
               session_.status.value().rfind("Unable to load", 0) == 0) {
        session_.status.value().clear();
    }
}

void CatalogController::open_share_code(uint32_t now)
{
    share_code_.open(now);
    session_.screen = Screen::ShareCode;
}

void CatalogController::submit_share_code()
{
    const std::string code = match_key(share_code_.input());
    if (code.empty()) {
        share_code_.message() = "Type a share code first.";
        return;
    }
    if (session_.catalog.apps().empty()) {
        share_code_.message() = "Catalog is still loading.";
        if (request_summary_) request_summary_();
        return;
    }
    for (int index = 0; index < static_cast<int>(session_.catalog.apps().size()); ++index) {
        const StoreApp &app = session_.catalog.apps()[index];
        if (match_key(app.share_code) != code && match_key(app.id) != code &&
            match_key(app.name) != code) continue;
        if (select_app_index(index)) {
            session_.status.value().clear();
            share_code_.message().clear();
            session_.screen = Screen::Detail;
            return;
        }
    }
    share_code_.message() = "No app found for code: " + share_code_.input();
}

void CatalogController::open_search()
{
    session_.search.reset();
    session_.screen = Screen::Search;
}

void CatalogController::submit_search()
{
    const bool catalog_empty = session_.catalog.apps().empty();
    const auto result = session_.search.submit(session_.catalog.apps());
    if (catalog_empty && !session_.search.input().empty()) {
        if (request_summary_) request_summary_();
        return;
    }
    if (result && select_app_index(*result)) {
        session_.status.value().clear();
        session_.screen = Screen::Detail;
    } else if (result) {
        session_.search.message() = "Unable to open result.";
    }
}

void CatalogController::open_selected_search_result()
{
    const auto result = session_.search.selected_result();
    if (!result) {
        submit_search();
        return;
    }
    if (select_app_index(*result)) {
        session_.search.clear_results();
        session_.status.value().clear();
        session_.search.message().clear();
        session_.screen = Screen::Detail;
    }
}

} // namespace appstore_ui
