#pragma once

#include "keyboard_input.h"

#include "appstore_session_state.hpp"
#include "catalog_controller.hpp"
#include "detail_action_controller.hpp"
#include "exit_controller.hpp"
#include "package_job_state.hpp"
#include "registry_controller.hpp"
#include "share_code_state.hpp"
#include "sync_controller.hpp"

#include <cstdint>
#include <functional>

namespace appstore_ui {

constexpr cp0_keyboard_input_context_t appstore_input_context(Screen screen)
{
    return screen == Screen::Search || screen == Screen::ShareCode ||
                   screen == Screen::RegistryEdit
               ? KBD_INPUT_CONTEXT_TEXT
               : KBD_INPUT_CONTEXT_NAVIGATION;
}

struct AppStoreKeyEvent {
    uint32_t code = 0;
    uint32_t mods = 0;
    char ch = 0;
    bool release = false;
    bool repeated = false;
};

class AppStoreInputController
{
public:
    struct Actions {
        std::function<void()> request_quit;
        std::function<void()> navigate_back;
        std::function<void()> open_registry;
        std::function<void()> open_registry_add;
        std::function<void()> execute_confirmation;
        std::function<void()> repair_package;
        std::function<void()> render;
    };

    AppStoreInputController(AppStoreSessionState &session, PackageJobState &package_job,
                            ExitController &exit, ShareCodeState &share_code,
                            CatalogController &catalog, DetailActionController &detail,
                            RegistryController &registry, SyncController &sync,
                            Actions actions);

    void handle(const AppStoreKeyEvent &key, uint32_t now);

private:
    static bool matches(const AppStoreKeyEvent &key, char ch, uint32_t code);

    AppStoreSessionState &session_;
    PackageJobState &package_job_;
    ExitController &exit_;
    ShareCodeState &share_code_;
    CatalogController &catalog_;
    DetailActionController &detail_;
    RegistryController &registry_;
    SyncController &sync_;
    Actions actions_;
};

} // namespace appstore_ui
