#include "catalog_controller.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>

namespace appstore {

std::string one_line(std::string value, size_t max_len)
{
    if (value.size() > max_len) value.resize(max_len);
    return value;
}

void sort_apps(std::vector<StoreApp> &apps, SortRule rule)
{
    if (rule == SortRule::Default) return;
    std::stable_sort(apps.begin(), apps.end(), [](const StoreApp &left, const StoreApp &right) {
        return left.name < right.name;
    });
}

} // namespace appstore

int main()
{
    using namespace appstore_ui;
    AppStoreSessionState session;
    ShareCodeState share_code;
    int refreshes = 0;
    CatalogController controller(session, share_code, [&]() { ++refreshes; });

    assert(controller.ensure_selected() == nullptr);
    assert(refreshes == 1);
    assert(!controller.open_detail());
    assert(session.status.value() == "Catalog is still loading");

    auto make_app = [](const char *id, const char *code, const char *name,
                       const char *category) {
        appstore::StoreApp app;
        app.id = id;
        app.share_code = code;
        app.name = name;
        app.version = "1";
        app.category = category;
        app.recommended = true;
        return app;
    };
    appstore::SummaryData summary;
    summary.saw_meta = true;
    summary.categories = {"Recommended", "All", "Tools"};
    summary.apps = {make_app("b-id", "BBBB", "Beta", "Tools"),
                    make_app("a-id", "AAAA", "Alpha", "Tools"),
                    make_app("c-id", "CCCC", "Gamma", "Games")};
    summary.repo_status = "3 apps";
    controller.apply_summary(summary);
    assert(session.catalog.current_category_name() == "All");
    assert(session.catalog.visible().size() == 3);
    assert(controller.open_detail() && session.screen == Screen::Detail);

    controller.select_adjacent_app(-1);
    assert(session.catalog.selected_index() == 2);
    controller.select_adjacent_app(1);
    assert(session.catalog.selected_index() == 0);
    controller.select_adjacent_category(1);
    assert(session.catalog.current_category_name() == "Tools");
    controller.select_adjacent_category(1);
    assert(session.catalog.current_category_name() == "Recommended");

    session.catalog.select_category_by_name("All");
    session.catalog.rebuild_visible();
    assert(controller.select_visible_app_by_id("b-id"));
    controller.cycle_sort_rule();
    assert(session.catalog.sort_rule() == appstore::SortRule::New);
    assert(session.catalog.selected_app()->id == "b-id");

    controller.open_share_code(1000);
    share_code.input() = "aaaa";
    controller.submit_share_code();
    assert(session.screen == Screen::Detail);
    assert(session.catalog.selected_app()->id == "a-id");

    controller.open_search();
    session.search.input() = "Tools";
    controller.submit_search();
    assert(session.search.results_active());
    assert(session.search.results().size() == 2);
    session.search.move_selection(1);
    controller.open_selected_search_result();
    assert(session.screen == Screen::Detail);
    assert(session.catalog.selected_app()->category == "Tools");
    assert(!session.search.results_active());

    controller.open_share_code(2000);
    share_code.input().clear();
    controller.submit_share_code();
    assert(share_code.message() == "Type a share code first.");

    std::cout << "catalog controller tests passed\n";
}
