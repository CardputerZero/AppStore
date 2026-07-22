#include "detail_media_state.hpp"

#include <algorithm>

namespace appstore_ui {

void DetailMediaState::normalize_images(const std::string &app_id, int image_count)
{
    if (image_app_id_ != app_id) {
        image_app_id_ = app_id;
        image_index_ = 0;
    }
    if (image_count <= 0) image_index_ = 0;
    else {
        if (image_index_ < 0) image_index_ = image_count - 1;
        if (image_index_ >= image_count) image_index_ = 0;
    }
}

void DetailMediaState::normalize_description(const std::string &app_id, int line_count,
                                             int visible_lines)
{
    if (description_app_id_ != app_id) {
        description_app_id_ = app_id;
        description_scroll_ = 0;
    }
    const int max_scroll = std::max(0, line_count - visible_lines);
    description_scroll_ = std::max(0, std::min(max_scroll, description_scroll_));
}

void DetailMediaState::cycle_image(int delta, int image_count)
{
    if (image_count <= 0) return;
    image_index_ = (image_index_ + delta) % image_count;
    if (image_index_ < 0) image_index_ += image_count;
}

void DetailMediaState::scroll_description(int delta, int line_count, int visible_lines)
{
    const int max_scroll = std::max(0, line_count - visible_lines);
    description_scroll_ = std::max(0, std::min(max_scroll, description_scroll_ + delta));
}

void DetailMediaState::show_overlay(uint32_t now)
{
    overlay_visible_ = true;
    overlay_activity_tick_ = now;
}

bool DetailMediaState::hide_overlay_if_elapsed(uint32_t now, uint32_t timeout_ms)
{
    if (loading_ || !overlay_visible_ || static_cast<uint32_t>(now - overlay_activity_tick_) < timeout_ms)
        return false;
    overlay_visible_ = false;
    return true;
}

void DetailMediaState::begin_loading(const std::string &app_id)
{
    image_app_id_ = app_id;
    image_index_ = 0;
    loading_ = true;
    load_failed_ = false;
}

void DetailMediaState::finish_loading(const std::string &app_id, bool failed)
{
    if (image_app_id_ != app_id) return;
    loading_ = false;
    load_failed_ = failed;
}

} // namespace appstore_ui
