#pragma once

#include "appstore_client.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace appstore_ui {

class RegistryUiState
{
public:
    static constexpr const char *kDefaultRegistryUrl =
        "https://cardputerzero.github.io/generated/registry.json";

    void apply(const appstore::RegistryData &data);
    void apply_config_hint(const appstore::RegistryConfig &config);
    void apply_region(const appstore::RegionData &region);
    void apply_local_region(const std::string &region);

    appstore::RegistryEntry *selected_entry();
    const appstore::RegistryEntry *selected_entry() const;
    void select_adjacent_entry(int delta);
    std::string adjacent_region(int delta) const;

    void begin_add();
    bool begin_edit_selected();
    void finish_edit();

    bool schedule_region(const std::string &region, uint32_t now);
    std::optional<std::string> take_due_region(uint32_t now, uint32_t delay_ms,
                                               bool operation_running);

    std::vector<appstore::RegistryEntry> &entries() { return entries_; }
    const std::vector<appstore::RegistryEntry> &entries() const { return entries_; }
    const std::vector<std::string> &lines() const { return lines_; }
    int &selected_index() { return selected_index_; }
    int selected_index() const { return selected_index_; }
    int &page_focus() { return page_focus_; }
    int page_focus() const { return page_focus_; }
    std::string &input_url() { return input_url_; }
    const std::string &input_url() const { return input_url_; }
    std::string &edit_url() { return edit_url_; }
    const std::string &edit_url() const { return edit_url_; }
    std::string &name_input() { return name_input_; }
    const std::string &name_input() const { return name_input_; }
    int &edit_focus() { return edit_focus_; }
    int edit_focus() const { return edit_focus_; }
    const std::string &region_code() const { return region_code_; }
    const std::string &region_label() const { return region_label_; }
    const std::string &registry_url() const { return registry_url_; }
    const std::string &active_region() const { return active_region_; }
    bool region_commit_pending() const { return region_commit_pending_; }

private:
    static std::string label_for_region(const std::string &region);
    std::string url_for_region(const std::string &region) const;
    void clamp_selection();

    std::vector<std::string> lines_;
    std::vector<appstore::RegistryEntry> entries_;
    int selected_index_ = 0;
    int page_focus_ = 0;
    std::string input_url_ = kDefaultRegistryUrl;
    std::string edit_url_;
    std::string name_input_;
    int edit_focus_ = 0;
    std::string region_code_ = "auto";
    std::string region_label_ = "Auto";
    std::string registry_url_ = kDefaultRegistryUrl;
    std::string active_region_ = "default";
    bool region_commit_pending_ = false;
    std::string pending_region_;
    uint32_t region_change_tick_ = 0;
};

} // namespace appstore_ui
