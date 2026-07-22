#include "share_code_state.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::ShareCodeState state;
    state.open(1000);
    assert(state.input().empty());
    assert(!state.append('c', 1100));
    assert(state.append('c', 1250));
    assert(state.append('1', 1251));
    assert(state.input() == "c1");
    assert(state.erase_last() && state.input() == "c");
    state.open(0xfffffff0U);
    assert(!state.append('c', 0x20U));
    std::cout << "share code state tests passed\n";
}
