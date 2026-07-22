#pragma once

#include <cstdint>
#include <string>

namespace appstore_ui {

class DetailMediaState
{
public:
    void normalize_images(const std::string &app_id, int image_count);
    void normalize_description(const std::string &app_id, int line_count, int visible_lines);
    void cycle_image(int delta, int image_count);
    void scroll_description(int delta, int line_count, int visible_lines);
    void show_overlay(uint32_t now);
    bool hide_overlay_if_elapsed(uint32_t now, uint32_t timeout_ms);
    void begin_loading(const std::string &app_id);
    void finish_loading(const std::string &app_id, bool failed);

    int image_index() const { return image_index_; }
    int description_scroll() const { return description_scroll_; }
    bool overlay_visible() const { return overlay_visible_; }
    uint32_t overlay_activity_tick() const { return overlay_activity_tick_; }
    bool loading() const { return loading_; }
    bool load_failed() const { return load_failed_; }

private:
    std::string image_app_id_;
    int image_index_ = 0;
    std::string description_app_id_;
    int description_scroll_ = 0;
    uint32_t overlay_activity_tick_ = 0;
    bool overlay_visible_ = true;
    bool loading_ = false;
    bool load_failed_ = false;
};

} // namespace appstore_ui
