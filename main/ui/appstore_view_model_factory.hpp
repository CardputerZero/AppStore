#pragma once

#include "appstore_view_models.hpp"
#include "appstore_session_state.hpp"
#include "package_job_state.hpp"
#include "share_code_state.hpp"

#include <cstdint>

namespace appstore_ui {

class AppStoreViewModelFactory
{
public:
    AppStoreViewModelFactory(AppStoreSessionState &session, PackageJobState &package_job,
                             ShareCodeState &share_code);

    CatalogDisplayViewModel catalog(uint32_t now, uint32_t status_visible_ms) const;
    AppDetailViewModel detail(appstore::StoreApp *selected, uint32_t now,
                              uint32_t status_visible_ms);
    ConfirmationViewModel confirmation() const;
    PackageProgressViewModel progress(uint32_t now) const;
    ErrorDialogViewModel error() const;
    InitializationProgressViewModel initialization() const;
    StoreSettingsViewModel settings(bool operation_running) const;
    RegistryEditorViewModel registry_editor() const;
    TextEntryViewModel share_code() const;
    SearchPageViewModel search();

private:
    AppStoreSessionState &session_;
    PackageJobState &package_job_;
    ShareCodeState &share_code_;
};

} // namespace appstore_ui
