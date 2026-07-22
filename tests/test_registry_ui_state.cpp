#include "registry_ui_state.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::RegistryUiState state;
    assert(state.region_code() == "auto");
    assert(state.registry_url() == appstore_ui::RegistryUiState::kDefaultRegistryUrl);

    appstore::RegistryData data;
    data.region = {"default", "Default", appstore_ui::RegistryUiState::kDefaultRegistryUrl,
                   "default"};
    data.entries.push_back({appstore_ui::RegistryUiState::kDefaultRegistryUrl,
                            "CardputerZero Hub", "", "", "", "", "default", true, true});
    data.entries.push_back({"https://example.test/registry.json", "Example", "", "", "", "",
                            "", true, false});
    state.apply(data);
    assert(state.selected_entry()->builtin);
    state.select_adjacent_entry(-1);
    assert(state.selected_index() == 1);
    assert(state.begin_edit_selected());
    assert(state.name_input() == "Example");
    state.select_adjacent_entry(1);
    assert(!state.begin_edit_selected());

    assert(state.schedule_region("CN", 100));
    assert(state.region_label() == "China");
    assert(state.registry_url().find("oss-cn-shenzhen") != std::string::npos);
    assert(!state.take_due_region(2099, 2000, false));
    assert(!state.take_due_region(2100, 2000, true));
    auto due = state.take_due_region(2100, 2000, false);
    assert(due && *due == "CN");
    assert(!state.region_commit_pending());

    state.begin_add();
    assert(state.edit_url().empty());
    assert(state.name_input().empty());
    assert(state.input_url() == appstore_ui::RegistryUiState::kDefaultRegistryUrl);

    std::cout << "registry UI state tests passed\n";
}
