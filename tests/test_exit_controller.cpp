#include "exit_controller.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::ExitController state;
    state.reset();
    assert(!state.requested());
    state.request();
    assert(state.requested());
    state.worker_started();
    assert(state.worker_count() == 1);
    state.worker_finished();
    assert(state.worker_count() == 0);
    state.esc_pressed(100);
    assert(state.esc_released(200, 1200));
    state.esc_pressed(0xfffffff0U);
    assert(!state.consume_esc_hold(0x20U, 100));
    assert(state.consume_esc_hold(0x80U, 100));
    assert(!state.esc_released(0x90U, 100));
    std::cout << "exit controller tests passed\n";
}
