#pragma once

#include "appstore_client.hpp"

#include <functional>
#include <string>

namespace appstore {

class SharedRegistryStore
{
public:
    using GetString = std::function<std::string(const std::string &, const std::string &)>;
    using SetString = std::function<void(const std::string &, const std::string &)>;
    using Save = std::function<void()>;

    SharedRegistryStore(GetString get_string, SetString set_string, Save save,
                        std::string prefix = "appstore.registry.", int max_entries = 16);

    bool load(RegistryConfig &config) const;
    bool save(const RegistryConfig &config) const;

    static std::string pack_entry(const RegistryEntry &entry);
    static bool unpack_entry(const std::string &packed, RegistryEntry &entry);

private:
    std::string key(const std::string &suffix) const;

    GetString get_string_;
    SetString set_string_;
    Save save_;
    std::string prefix_;
    int max_entries_ = 16;
};

} // namespace appstore
