#include "confirmation_state.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::ConfirmationState state;
    state.begin("upgrade", "app-id");
    assert(state.action() == "upgrade");
    assert(state.app_id() == "app-id");
    assert(state.apply_plan("PLAN\tapp-id\tExample\t1.0\t12 MB\t80 MB\tlibfoo\t\n"));
    assert(state.lines().size() == 5);
    assert(state.lines()[0] == "Upgrade Example");
    assert(state.lines()[4] == "Install checks: OK");
    state.focus() = 1;
    state.reset();
    assert(state.action().empty() && state.app_id().empty() && state.lines().empty());
    assert(state.focus() == 0);
    assert(!state.apply_plan("ERROR\tbad plan\n"));
    std::cout << "confirmation state tests passed\n";
}
