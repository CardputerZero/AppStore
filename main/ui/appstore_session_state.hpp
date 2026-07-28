#pragma once


#include "appstore_client.hpp"
#include "appstore_task_coordinator.hpp"
#include "catalog_state.hpp"
#include "confirmation_state.hpp"
#include "detail_media_state.hpp"
#include "registry_ui_state.hpp"
#include "search_state.hpp"
#include "status_message_state.hpp"

#include <cstdint>
#include <string>

namespace appstore_ui {

struct ErrorDialogState {
    std::string title;
    std::string message;
    std::string detail;
    bool repairable = false;
    std::string repair_action;
    std::string repair_app_id;
    std::string repair_title;

    void clear()
    {
        title.clear();
        message.clear();
        detail.clear();
        repairable = false;
        repair_action.clear();
        repair_app_id.clear();
        repair_title.clear();
    }
};

struct SyncPresentationState {
    appstore::SyncStatus status;
    uint32_t visible_until_tick = 0;
    bool startup_active = false;
    bool startup_failed = false;
    int failure_focus = 0;
    int animation_phase = -1;
};

struct AppStoreSessionState {
    std::string app_dir = ".";
    StatusMessageState status;
    std::string repo_status = "built-in";
    std::string free_space = "-";
    std::string root_path = "-";
    CatalogState catalog;
    RegistryUiState registry;
    Screen screen = Screen::Home;
    ConfirmationState confirmation;
    SearchState search;
    DetailMediaState detail_media;
    bool registry_loading = false;
    ErrorDialogState error;
    SyncPresentationState sync;
};

} // namespace appstore_ui
