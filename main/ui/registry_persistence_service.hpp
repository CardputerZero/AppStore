#pragma once

#include "appstore_session_state.hpp"
#include "shared_registry_store.hpp"

#include <functional>
#include <string>

namespace appstore_ui {

class RegistryPersistenceService
{
public:
    struct Dependencies {
        std::function<appstore::RegistryConfig()> load_backend;
        std::function<bool(const appstore::RegistryConfig &)> replace_backend;
        std::function<void(const std::string &)> log;
    };

    RegistryPersistenceService(AppStoreSessionState &session,
                               appstore::SharedRegistryStore &shared_store,
                               Dependencies dependencies);

    void initialize();
    bool save_backend();

private:
    void apply_hint(const appstore::RegistryConfig &config);
    void log_config(const char *action, const appstore::RegistryConfig &config) const;

    AppStoreSessionState &session_;
    appstore::SharedRegistryStore &shared_store_;
    Dependencies dependencies_;
};

} // namespace appstore_ui
