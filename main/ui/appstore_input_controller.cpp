#include "appstore_input_controller.hpp"

#include "appstore_client.hpp"
#include "input_keys.h"
#include "startup_network_flow.hpp"

#include <utility>

namespace appstore_ui {

namespace {
constexpr uint32_t kEscLongPressMs = 1200;
}

AppStoreInputController::AppStoreInputController(
    AppStoreSessionState &session, PackageJobState &package_job, ExitController &exit,
    ShareCodeState &share_code, CatalogController &catalog,
    DetailActionController &detail, RegistryController &registry, SyncController &sync,
    Actions actions)
    : session_(session), package_job_(package_job), exit_(exit), share_code_(share_code),
      catalog_(catalog), detail_(detail), registry_(registry), sync_(sync),
      actions_(std::move(actions))
{
}

bool AppStoreInputController::matches(const AppStoreKeyEvent &key, char ch,
                                      uint32_t code)
{
    return key.ch == ch || key.code == code;
}

void AppStoreInputController::handle(const AppStoreKeyEvent &key, uint32_t now)
{
    if (exit_.requested()) return;
    if (key.code == KEY_ESC) {
        if (key.release) {
            if (exit_.esc_released(now, kEscLongPressMs) &&
                session_.screen != Screen::StartupSync) {
                if (actions_.navigate_back) actions_.navigate_back();
                if (actions_.render) actions_.render();
            }
        } else if (!key.repeated) {
            exit_.esc_pressed(now);
        } else if (exit_.consume_esc_hold(now, kEscLongPressMs)) {
            if (actions_.request_quit) actions_.request_quit();
        }
        return;
    }
    if (key.release) return;

    switch (session_.screen) {
        case Screen::StartupSync:
            if (!session_.sync.startup_failed) break;
            if (key.code == KEY_LEFT || key.code == KEY_RIGHT || key.code == KEY_TAB ||
                key.code == KEY_Z || key.code == KEY_C || key.ch == 'z' || key.ch == 'c') {
                session_.sync.failure_focus = 1 - session_.sync.failure_focus;
            } else if (key.code == KEY_ENTER) {
                if (session_.sync.failure_focus == 0) {
                    if (actions_.request_quit) actions_.request_quit();
                } else if (actions_.open_registry) {
                    actions_.open_registry();
                }
            }
            break;
        case Screen::Home:
            if ((key.code == KEY_UP || key.code == KEY_F || key.ch == 'f') &&
                !session_.catalog.visible().empty())
                catalog_.select_adjacent_app(-1);
            else if ((key.code == KEY_DOWN || key.code == KEY_X || key.ch == 'x') &&
                     !session_.catalog.visible().empty())
                catalog_.select_adjacent_app(1);
            else if ((key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<') &&
                     !session_.catalog.categories().empty())
                catalog_.select_adjacent_category(-1);
            else if ((key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>') &&
                     !session_.catalog.categories().empty())
                catalog_.select_adjacent_category(1);
            else if (matches(key, '4', KEY_4) || matches(key, 's', KEY_S)) {
                if (actions_.open_registry) actions_.open_registry();
            } else if (matches(key, '5', KEY_5))
                catalog_.open_share_code(now);
            else if (matches(key, '6', KEY_6))
                catalog_.open_search();
            else if (matches(key, '7', KEY_7)) {
                appstore::StoreApp *app = session_.catalog.selected_app();
                if (app && appstore::can_install_app(*app))
                    detail_.start_confirmation(app->installed ? "reinstall" : "install");
                else
                    session_.status.value() = app ? "Only approved apps can install" : "No selected app";
            } else if (matches(key, '8', KEY_8) || key.code == KEY_ENTER) {
                if (session_.catalog.selected_app()) catalog_.open_detail();
            }
            else if (key.code == KEY_TAB)
                catalog_.cycle_sort_rule();
            else if (matches(key, 'q', KEY_Q) && actions_.request_quit)
                actions_.request_quit();
            break;
        case Screen::Detail: {
            appstore::StoreApp *app = catalog_.ensure_selected();
            if (key.code == KEY_UP || key.code == KEY_F || key.ch == 'f')
                detail_.scroll_description(-1);
            else if (key.code == KEY_DOWN || key.code == KEY_X || key.ch == 'x')
                detail_.scroll_description(1);
            else if (matches(key, '4', KEY_4) || matches(key, 'b', KEY_B))
                session_.screen = Screen::Home;
            else if (matches(key, '5', KEY_5))
                detail_.open_screenshots(now);
            else if (matches(key, '6', KEY_6)) {
                if (app && appstore::can_reinstall_app(*app))
                    detail_.reinstall(app);
                else if (app && !app->installed && appstore::can_install_app(*app))
                    detail_.install(app);
            } else if (matches(key, '7', KEY_7)) {
                if (app && appstore::can_upgrade_app(*app)) detail_.upgrade(app);
            } else if (matches(key, '8', KEY_8)) {
                if (app && app->installed) detail_.remove(app);
            }
            else if (app && matches(key, 'i', KEY_I)) {
                if (appstore::can_install_app(*app))
                    detail_.start_confirmation(app->installed ? "reinstall" : "install");
                else
                    session_.status.value() = "Only approved apps can install";
            } else if (app && app->installed && appstore::can_install_app(*app) &&
                       matches(key, 'u', KEY_U))
                detail_.start_confirmation("upgrade");
            else if (app && app->installed && matches(key, 'd', KEY_D))
                detail_.start_confirmation("uninstall");
            break;
        }
        case Screen::Screenshots:
            if (key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<')
                detail_.cycle_screenshot(-1, now);
            else if (key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>')
                detail_.cycle_screenshot(1, now);
            else if (matches(key, '4', KEY_4) || matches(key, 'b', KEY_B))
                session_.screen = Screen::Detail;
            break;
        case Screen::Confirm:
            if (matches(key, 'b', KEY_B) || matches(key, 'n', KEY_N))
                detail_.cancel_confirmation();
            else if (matches(key, 'y', KEY_Y)) {
                if (actions_.execute_confirmation) actions_.execute_confirmation();
            } else if (key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<')
                session_.confirmation.focus() = 0;
            else if (key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>')
                session_.confirmation.focus() = 1;
            else if (key.code == KEY_TAB)
                session_.confirmation.focus() = 1 - session_.confirmation.focus();
            else if (key.code == KEY_ENTER) {
                if (session_.confirmation.focus() == 0) {
                    if (actions_.execute_confirmation) actions_.execute_confirmation();
                } else {
                    detail_.cancel_confirmation();
                }
            }
            break;
        case Screen::Progress:
            if (!package_job_.running && !package_job_.pending_start && matches(key, 'b', KEY_B))
                session_.screen = Screen::Detail;
            break;
        case Screen::ErrorDialog:
            if (session_.error.repairable &&
                (key.code == KEY_ENTER || matches(key, 'r', KEY_R))) {
                if (actions_.repair_package) actions_.repair_package();
            } else if (key.code == KEY_ENTER || matches(key, 'y', KEY_Y) || matches(key, 'b', KEY_B))
                if (actions_.navigate_back) actions_.navigate_back();
            break;
        case Screen::Registry:
            if (matches(key, 'b', KEY_B)) {
                if (actions_.navigate_back) actions_.navigate_back();
            }
            else if (key.code == KEY_UP || key.code == KEY_DOWN)
                session_.registry.page_focus() = 1 - session_.registry.page_focus();
            else if (matches(key, '5', KEY_5) || matches(key, 'e', KEY_E) ||
                       (key.code == KEY_ENTER && session_.registry.page_focus() == 1)) {
                const auto *entry = session_.registry.selected_entry();
                if (entry && !entry->builtin) registry_.edit_selected();
            } else if (matches(key, '7', KEY_7) || matches(key, 'd', KEY_D)) {
                const auto *entry = session_.registry.selected_entry();
                if (entry && !entry->builtin) registry_.delete_selected();
            } else if (matches(key, '8', KEY_8) || matches(key, 'r', KEY_R)) {
                sync_.start(true, now);
            } else if (key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<') {
                if (session_.registry.page_focus() == 0)
                    registry_.select_region(registry_.adjacent_region(-1), now);
                else
                    session_.registry.select_adjacent_entry(-1);
            } else if (key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>') {
                if (session_.registry.page_focus() == 0)
                    registry_.select_region(registry_.adjacent_region(1), now);
                else
                    session_.registry.select_adjacent_entry(1);
            }
            break;
        case Screen::RegistryEdit: {
            std::string &field = session_.registry.edit_focus() == 0 ?
                session_.registry.name_input() : session_.registry.input_url();
            if (key.code == KEY_BACKSPACE && !field.empty())
                field.pop_back();
            else if (key.code == KEY_ENTER)
                registry_.submit_editor();
            else if (key.code == KEY_TAB || key.code == KEY_UP || key.code == KEY_DOWN)
                session_.registry.edit_focus() = 1 - session_.registry.edit_focus();
            else if (key.ch >= 32 && key.ch <= 126 &&
                     (session_.registry.edit_focus() == 0 ?
                      session_.registry.name_input().size() < 48 :
                      session_.registry.input_url().size() < 180))
                field.push_back(key.ch);
            else if (key.code == KEY_LEFT && !session_.registry.edit_url().empty() &&
                     !session_.registry.entries().empty()) {
                session_.registry.select_adjacent_entry(-1);
                registry_.edit_selected();
            } else if (key.code == KEY_RIGHT && !session_.registry.edit_url().empty() &&
                       !session_.registry.entries().empty()) {
                session_.registry.select_adjacent_entry(1);
                registry_.edit_selected();
            }
            break;
        }
        case Screen::ShareCode:
            if (key.code == KEY_BACKSPACE)
                share_code_.erase_last();
            else if (key.code == KEY_ENTER)
                catalog_.submit_share_code();
            else
                share_code_.append(key.ch, now);
            break;
        case Screen::Search:
            if (session_.search.results_active() && !session_.search.results().empty() &&
                (key.code == KEY_UP || key.code == KEY_F || key.ch == 'f'))
                session_.search.move_selection(-1);
            else if (session_.search.results_active() && !session_.search.results().empty() &&
                     (key.code == KEY_DOWN || key.code == KEY_X || key.ch == 'x'))
                session_.search.move_selection(1);
            else if (key.code == KEY_BACKSPACE && !session_.search.input().empty()) {
                session_.search.input().pop_back();
                session_.search.input_changed();
            } else if (key.code == KEY_ENTER)
                catalog_.open_selected_search_result();
            else if (key.ch >= 32 && key.ch <= 126 && session_.search.input().size() < 64) {
                session_.search.input().push_back(key.ch);
                session_.search.input_changed();
            }
            break;
    }
    if (actions_.render) actions_.render();
}

} // namespace appstore_ui
