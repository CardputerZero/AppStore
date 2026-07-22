#include "registry_ui_state.hpp"

#include <algorithm>

namespace appstore_ui {
namespace {

constexpr const char *kChinaRegistryUrl =
    "https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json";

} // namespace

void RegistryUiState::apply(const appstore::RegistryData &data)
{
    apply_region(data.region);
    entries_ = data.entries;
    lines_ = data.lines;
    clamp_selection();
}

void RegistryUiState::apply_config_hint(const appstore::RegistryConfig &config)
{
    region_code_ = config.region.empty() ? "auto" : config.region;
    region_label_ = label_for_region(region_code_);
    active_region_ = config.active_region.empty() ? "default" : config.active_region;
    for (const auto &entry : config.entries) {
        if (entry.builtin || entry.region == active_region_) {
            registry_url_ = entry.url;
            return;
        }
    }
    if (!config.entries.empty()) registry_url_ = config.entries.front().url;
}

void RegistryUiState::apply_region(const appstore::RegionData &region)
{
    region_code_ = region.code.empty() ? "auto" : region.code;
    region_label_ = region.label.empty() ? label_for_region(region_code_) : region.label;
    registry_url_ = region.registry_url.empty() ? url_for_region(region_code_) : region.registry_url;
    active_region_ = region.active.empty() ? "default" : region.active;
}

void RegistryUiState::apply_local_region(const std::string &region)
{
    region_code_ = region;
    region_label_ = label_for_region(region);
    registry_url_ = url_for_region(region);
    if (region != "auto") active_region_ = region;
    for (auto &entry : entries_) {
        if (entry.builtin || entry.region == "CN" || entry.region == "default") {
            const bool name_follows_url = entry.name.empty() || entry.name == entry.url;
            entry.url = registry_url_;
            entry.region = region == "auto" ? active_region_ : region;
            if (name_follows_url) entry.name = "CardputerZero Hub";
        }
    }
}

appstore::RegistryEntry *RegistryUiState::selected_entry()
{
    return selected_index_ >= 0 && selected_index_ < static_cast<int>(entries_.size())
        ? &entries_[selected_index_] : nullptr;
}

const appstore::RegistryEntry *RegistryUiState::selected_entry() const
{
    return selected_index_ >= 0 && selected_index_ < static_cast<int>(entries_.size())
        ? &entries_[selected_index_] : nullptr;
}

void RegistryUiState::select_adjacent_entry(int delta)
{
    if (entries_.empty()) return;
    const int count = static_cast<int>(entries_.size());
    selected_index_ = (selected_index_ + delta) % count;
    if (selected_index_ < 0) selected_index_ += count;
}

std::string RegistryUiState::adjacent_region(int delta) const
{
    static const std::vector<std::string> regions = {"auto", "default", "CN"};
    auto it = std::find(regions.begin(), regions.end(), region_code_);
    int index = it == regions.end() ? 0 : static_cast<int>(it - regions.begin());
    int next = (index + delta) % static_cast<int>(regions.size());
    if (next < 0) next += static_cast<int>(regions.size());
    return regions[next];
}

void RegistryUiState::begin_add()
{
    edit_url_.clear();
    name_input_.clear();
    input_url_ = kDefaultRegistryUrl;
    edit_focus_ = 0;
}

bool RegistryUiState::begin_edit_selected()
{
    const auto *entry = selected_entry();
    if (!entry || entry->builtin) return false;
    edit_url_ = entry->url;
    input_url_ = entry->url;
    name_input_ = entry->name;
    edit_focus_ = 0;
    return true;
}

void RegistryUiState::finish_edit()
{
    edit_url_.clear();
}

bool RegistryUiState::schedule_region(const std::string &region, uint32_t now)
{
    if (region == region_code_) return false;
    apply_local_region(region);
    pending_region_ = region;
    region_commit_pending_ = true;
    region_change_tick_ = now;
    return true;
}

std::optional<std::string> RegistryUiState::take_due_region(uint32_t now, uint32_t delay_ms,
                                                            bool operation_running)
{
    if (!region_commit_pending_ || operation_running ||
        static_cast<uint32_t>(now - region_change_tick_) < delay_ms) return std::nullopt;
    region_commit_pending_ = false;
    return pending_region_;
}

std::string RegistryUiState::label_for_region(const std::string &region)
{
    if (region == "CN") return "China";
    if (region == "default") return "Default";
    return "Auto";
}

std::string RegistryUiState::url_for_region(const std::string &region) const
{
    if (region == "CN" || (region == "auto" && active_region_ == "CN"))
        return kChinaRegistryUrl;
    return kDefaultRegistryUrl;
}

void RegistryUiState::clamp_selection()
{
    if (selected_index_ >= static_cast<int>(entries_.size()))
        selected_index_ = std::max(0, static_cast<int>(entries_.size()) - 1);
    if (selected_index_ < 0) selected_index_ = 0;
}

} // namespace appstore_ui
