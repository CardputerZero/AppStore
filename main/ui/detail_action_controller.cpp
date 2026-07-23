#include "detail_action_controller.hpp"

#include "appstore_paths.hpp"

#include <sstream>
#include <utility>

namespace appstore_ui {

using namespace appstore;

DetailActionController::DetailActionController(AppStoreSessionState &session,
                                               PackageJobState &package_job,
                                               PlanStarter start_plan,
                                               PlanRunning plan_running)
    : session_(session), package_job_(package_job), start_plan_(std::move(start_plan)),
      plan_running_(std::move(plan_running))
{
}

std::vector<std::string> DetailActionController::description_lines(const StoreApp &app)
{
    return wrap_display_text(app.description.empty() ? "-" : app.description, 34);
}

StoreApp *DetailActionController::selected_app()
{
    StoreApp *app = session_.catalog.selected_app();
    if (!app) session_.catalog.rebuild_visible();
    return session_.catalog.selected_app();
}

void DetailActionController::start_confirmation(const std::string &action)
{
    StoreApp *app = session_.catalog.selected_app();
    if (!app) return;
    if (package_job_.running) {
        session_.status.value() = "Operation already running";
        return;
    }
    if (plan_running_ && plan_running_()) {
        session_.status.value() = "Plan check already running";
        return;
    }
    session_.confirmation.begin(action, app->id);
    if (action == "uninstall") {
        session_.confirmation.lines() = {"Delete " + app->name,
                                         "Remove the installed application.",
                                         "Application data may remain on disk."};
        session_.screen = Screen::Confirm;
        return;
    }
    if (start_plan_) start_plan_(action, app->id);
}

void DetailActionController::install(StoreApp *app)
{
    if (!app) session_.status.value() = "No selected app";
    else if (app->installed) session_.status.value() = "Already installed";
    else if (!can_install_app(*app)) session_.status.value() = "Only approved apps can install";
    else start_confirmation("install");
}

void DetailActionController::reinstall(StoreApp *app)
{
    if (!app) session_.status.value() = "No selected app";
    else if (!app->installed) session_.status.value() = "App is not installed";
    else if (!can_reinstall_app(*app)) session_.status.value() = "Only approved apps can install";
    else start_confirmation("reinstall");
}

void DetailActionController::upgrade(StoreApp *app)
{
    if (!app) session_.status.value() = "No selected app";
    else if (!app->installed) session_.status.value() = "App is not installed";
    else if (!can_install_app(*app)) session_.status.value() = "Only approved apps can install";
    else if (!can_upgrade_app(*app)) session_.status.value() = "Already latest";
    else start_confirmation("upgrade");
}

void DetailActionController::remove(StoreApp *app)
{
    if (!app) session_.status.value() = "No selected app";
    else if (!app->installed) session_.status.value() = "App is not installed";
    else start_confirmation("uninstall");
}

void DetailActionController::cancel_confirmation()
{
    session_.screen = Screen::Detail;
    session_.confirmation.reset();
}

bool DetailActionController::execute_confirmation(uint32_t now)
{
    StoreApp *app = nullptr;
    for (auto &candidate : session_.catalog.apps()) {
        if (candidate.id == session_.confirmation.app_id()) {
            app = &candidate;
            break;
        }
    }
    if (!app || session_.confirmation.action().empty() || package_job_.running) return false;
    const std::string action = session_.confirmation.action();
    const bool force_overwrite = session_.confirmation.force_overwrite();
    session_.confirmation.reset();
    package_job_.begin(action, app->id, app->name, now, force_overwrite);
    session_.status.value() = "Preparing " + one_line(app->name, 18) + "...";
    session_.screen = Screen::Progress;
    return true;
}

bool DetailActionController::apply_plan(const std::string &output)
{
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        const auto fields = split_tab(line);
        if (fields.size() < 8 || fields[0] != "PLAN") continue;
        if (has_blocking_missing(fields[7])) {
            session_.status.value() = missing_install_message(fields[7]);
            return false;
        }
        return session_.confirmation.apply_plan(output);
    }
    return false;
}

bool DetailActionController::apply_plan_result(const std::string &action,
                                               const std::string &app_id,
                                               Screen origin_screen, int rc,
                                               const std::string &output)
{
    session_.confirmation.begin(action, app_id);
    StoreApp *current = session_.catalog.selected_app();
    if (session_.screen != origin_screen || !current || current->id != app_id) {
        session_.confirmation.reset();
        session_.status.value() = "Install plan expired";
        return false;
    }
    if (rc == 0 && apply_plan(output)) {
        session_.status.value().clear();
        session_.screen = Screen::Confirm;
        return true;
    }
    session_.status.value() = one_line(backend_error_message(output), 44);
    return false;
}

void DetailActionController::cycle_screenshot(int delta, uint32_t now)
{
    StoreApp *app = selected_app();
    if (!app) {
        session_.status.value() = "No selected app";
        return;
    }
    const auto screenshots = detail_screenshot_paths(session_.app_dir, *app);
    session_.detail_media.normalize_images(app->id, static_cast<int>(screenshots.size()));
    if (screenshots.empty()) {
        session_.status.value() = "No screenshots for this app";
        return;
    }
    session_.detail_media.cycle_image(delta, static_cast<int>(screenshots.size()));
    session_.status.value().clear();
    session_.detail_media.show_overlay(now);
}

bool DetailActionController::open_screenshots(uint32_t now)
{
    StoreApp *app = selected_app();
    if (!app) {
        session_.status.value() = "No selected app";
        return false;
    }
    session_.status.value().clear();
    session_.detail_media.show_overlay(now);
    session_.screen = Screen::Screenshots;
    const auto screenshots = detail_screenshot_paths(session_.app_dir, *app);
    session_.detail_media.normalize_images(app->id, static_cast<int>(screenshots.size()));
    if (screenshots.empty() && start_screenshots_) {
        session_.detail_media.begin_loading(app->id);
        if (!start_screenshots_(app->id))
            session_.detail_media.finish_loading(app->id, true);
    }
    return true;
}

void DetailActionController::scroll_description(int delta)
{
    StoreApp *app = selected_app();
    if (!app) {
        session_.status.value() = "No selected app";
        return;
    }
    auto lines = description_lines(*app);
    session_.detail_media.normalize_description(app->id, static_cast<int>(lines.size()), 3);
    if (lines.size() <= 3) return;
    session_.detail_media.scroll_description(delta, static_cast<int>(lines.size()), 3);
    session_.status.value().clear();
}

} // namespace appstore_ui
