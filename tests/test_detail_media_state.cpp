#include "detail_media_state.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::DetailMediaState state;
    state.normalize_images("a", 3);
    state.cycle_image(-1, 3);
    assert(state.image_index() == 2);
    state.normalize_images("b", 2);
    assert(state.image_index() == 0);
    state.normalize_description("a", 6, 2);
    state.scroll_description(20, 6, 2);
    assert(state.description_scroll() == 4);
    state.normalize_description("b", 6, 2);
    assert(state.description_scroll() == 0);
    state.show_overlay(0xfffffff0U);
    assert(!state.hide_overlay_if_elapsed(0x00000010U, 40));
    assert(state.hide_overlay_if_elapsed(0x00000020U, 40));
    assert(!state.overlay_visible());
    std::cout << "detail media state tests passed\n";
}
