#include "../main/interface/shared_registry_store.hpp"

#include <cassert>
#include <map>

int main()
{
    std::map<std::string, std::string> values;
    bool saved = false;
    appstore::SharedRegistryStore store(
        [&](const std::string &key, const std::string &fallback) {
            auto found = values.find(key);
            return found == values.end() ? fallback : found->second;
        },
        [&](const std::string &key, const std::string &value) { values[key] = value; },
        [&]() { saved = true; });

    appstore::RegistryConfig config;
    config.region = "CN";
    config.active_region = "CN";
    appstore::RegistryEntry entry;
    entry.name = "Name\twith slash \\";
    entry.url = "https://example.test/a\nb";
    entry.enabled = true;
    entry.builtin = false;
    entry.region = "CN";
    config.entries.push_back(entry);
    assert(store.save(config) && saved);

    appstore::RegistryConfig loaded;
    assert(store.load(loaded));
    assert(loaded.region == "CN" && loaded.active_region == "CN");
    assert(loaded.entries.size() == 1);
    assert(loaded.entries[0].name == entry.name);
    assert(loaded.entries[0].url == entry.url);
    assert(loaded.entries[0].enabled && !loaded.entries[0].builtin);
    return 0;
}
