#include "../main/ui/search_state.hpp"

#include <cassert>

int main()
{
    appstore_ui::SearchState state;
    std::vector<appstore::StoreApp> apps(3);
    apps[0].name = "Calculator";
    apps[1].name = "Card Game";
    apps[2].author = "Card Labs";

    state.input() = "calculator";
    assert(state.submit(apps) == 0);

    state.input() = "card";
    assert(!state.submit(apps));
    assert(state.results_active() && state.results().size() == 2);
    assert(state.selected_result() == 1);
    state.move_selection(1);
    assert(state.selected_result() == 2);
    state.move_selection(1);
    assert(state.selected_result() == 1);

    state.input().clear();
    assert(!state.submit(apps));
    assert(state.message() == "Type a search term first.");
    return 0;
}
