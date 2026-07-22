#include "appstore_session_state.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::AppStoreSessionState state;
    assert(!state.sync.startup_active);
    assert(!state.sync.startup_failed);
    assert(state.sync.failure_focus == 0);
    assert(state.sync.animation_phase == -1);
    state.error.title = "Failed";
    state.error.message = "Message";
    state.error.detail = "Detail";
    state.error.clear();
    assert(state.error.title.empty());
    assert(state.error.message.empty());
    assert(state.error.detail.empty());
    std::cout << "appstore session state tests passed\n";
}
