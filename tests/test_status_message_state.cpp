#include "../main/ui/status_message_state.hpp"

#include <cassert>

int main()
{
    appstore_ui::StatusMessageState state;
    assert(!state.visible(10));

    state.value() = "Loading";
    assert(state.visible(100, 50));
    assert(state.visible(149, 50));
    assert(!state.visible(150, 50));
    assert(state.value().empty());

    state.value() = "First";
    assert(state.visible(200, 50));
    state.value() = "Second";
    assert(state.visible(240, 50));
    assert(state.visible(289, 50));
    assert(!state.visible(290, 50));

    state.value() = "Wrap";
    assert(state.visible(0xFFFFFFF0u, 32));
    assert(state.visible(0x0000000Fu, 32));
    assert(!state.visible(0x00000010u, 32));
    return 0;
}
