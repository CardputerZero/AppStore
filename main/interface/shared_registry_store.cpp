#include "shared_registry_store.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

namespace appstore {
namespace {

std::string escape_field(const std::string &value)
{
    std::string output;
    output.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': output += "\\\\"; break;
            case '\t': output += "\\t"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            default: output += ch; break;
        }
    }
    return output;
}

std::vector<std::string> split_entry(const std::string &value)
{
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;
    for (char ch : value) {
        if (escaped) {
            if (ch == 't') current += '\t';
            else if (ch == 'n') current += '\n';
            else if (ch == 'r') current += '\r';
            else current += ch;
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    if (escaped) current += '\\';
    fields.push_back(current);
    return fields;
}

} // namespace

SharedRegistryStore::SharedRegistryStore(GetString get_string, SetString set_string, Save save,
                                         std::string prefix, int max_entries)
    : get_string_(std::move(get_string)), set_string_(std::move(set_string)),
      save_(std::move(save)), prefix_(std::move(prefix)), max_entries_(max_entries)
{
}

bool SharedRegistryStore::load(RegistryConfig &config) const
{
    const std::string count_text = get_string_(key("count"), "");
    if (count_text.empty()) return false;
    int count = std::min(std::atoi(count_text.c_str()), max_entries_);
    if (count <= 0) return false;

    config = RegistryConfig{};
    config.region = get_string_(key("region"), "auto");
    config.active_region = get_string_(key("active_region"), "default");
    for (int index = 0; index < count; ++index) {
        RegistryEntry entry;
        if (unpack_entry(get_string_(key(std::to_string(index) + ".entry"), ""), entry))
            config.entries.push_back(std::move(entry));
    }
    return !config.entries.empty();
}

bool SharedRegistryStore::save(const RegistryConfig &config) const
{
    const int count = std::min(static_cast<int>(config.entries.size()), max_entries_);
    set_string_(key("region"), config.region.empty() ? "auto" : config.region);
    set_string_(key("active_region"), config.active_region.empty() ? "default" : config.active_region);
    set_string_(key("count"), std::to_string(count));
    for (int index = 0; index < max_entries_; ++index)
        set_string_(key(std::to_string(index) + ".entry"),
                    index < count ? pack_entry(config.entries[index]) : "");
    save_();
    return true;
}

std::string SharedRegistryStore::pack_entry(const RegistryEntry &entry)
{
    return escape_field(entry.name) + "\t" + escape_field(entry.url) + "\t" +
        (entry.enabled ? "1" : "0") + "\t" + (entry.builtin ? "1" : "0") + "\t" +
        escape_field(entry.region);
}

bool SharedRegistryStore::unpack_entry(const std::string &packed, RegistryEntry &entry)
{
    const auto fields = split_entry(packed);
    if (fields.size() < 5 || fields[1].empty()) return false;
    entry.name = fields[0];
    entry.url = fields[1];
    entry.enabled = fields[2] != "0";
    entry.builtin = fields[3] == "1";
    entry.region = fields[4];
    return true;
}

std::string SharedRegistryStore::key(const std::string &suffix) const
{
    return prefix_ + suffix;
}

} // namespace appstore
