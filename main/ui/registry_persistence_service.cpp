#include "registry_persistence_service.hpp"

#include <utility>

namespace appstore_ui {

RegistryPersistenceService::RegistryPersistenceService(
    AppStoreSessionState &session, appstore::SharedRegistryStore &shared_store,
    Dependencies dependencies)
    : session_(session), shared_store_(shared_store),
      dependencies_(std::move(dependencies))
{
}

void RegistryPersistenceService::apply_hint(const appstore::RegistryConfig &config)
{
    session_.registry.apply_config_hint(config);
}

void RegistryPersistenceService::log_config(
    const char *action, const appstore::RegistryConfig &config) const
{
    if (!dependencies_.log) return;
    dependencies_.log(std::string("shared_registry ") + action + " entries=" +
                      std::to_string(config.entries.size()) + " region=" + config.region +
                      " active=" + config.active_region);
}

void RegistryPersistenceService::initialize()
{
    appstore::RegistryConfig shared;
    if (shared_store_.load(shared)) {
        log_config("startup import", shared);
        apply_hint(shared);
        if (dependencies_.replace_backend && !dependencies_.replace_backend(shared))
            session_.status.value() = "Unable to apply saved registry settings";
        return;
    }

    if (!dependencies_.load_backend) return;
    const appstore::RegistryConfig backend = dependencies_.load_backend();
    if (backend.entries.empty()) return;
    shared_store_.save(backend);
    apply_hint(backend);
    log_config("migrated", backend);
}

bool RegistryPersistenceService::save_backend()
{
    if (!dependencies_.load_backend) return false;
    const appstore::RegistryConfig backend = dependencies_.load_backend();
    if (backend.entries.empty()) {
        if (dependencies_.log) dependencies_.log("shared_registry save skipped: backend config empty");
        return false;
    }
    return shared_store_.save(backend);
}

} // namespace appstore_ui
