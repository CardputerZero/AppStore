#include "../main/ui/catalog_state.hpp"

#include <cassert>

int main()
{
    appstore_ui::CatalogState state;
    assert(state.current_category_name() == "Recommended");

    appstore::StoreApp recommended;
    recommended.id = "recommended";
    recommended.category = "Games";
    recommended.recommended = true;
    appstore::StoreApp utility;
    utility.id = "utility";
    utility.category = "Utilities";
    state.apps() = {recommended, utility};

    state.rebuild_visible();
    assert(state.visible().size() == 1);
    assert(state.selected_app() && state.selected_app()->id == "recommended");

    state.select_default_category();
    state.rebuild_visible();
    assert(state.current_category_name() == "All");
    assert(state.visible().size() == 2);

    state.selected_index() = 99;
    state.rebuild_visible();
    assert(state.selected_index() == 1);
    assert(state.selected_app() && state.selected_app()->id == "utility");

    state.categories().push_back("Games");
    assert(state.select_category_by_name("Games"));
    state.rebuild_visible();
    assert(state.visible().size() == 1);
    assert(state.selected_index() == 0);
    return 0;
}
