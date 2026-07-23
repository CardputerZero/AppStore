#include "appstore_view_model_factory.hpp"

#include "detail_action_controller.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace appstore_ui {

using namespace appstore;

namespace {

std::string human_size(const std::string &value)
{
    if (value.empty()) return "-";
    char *end = nullptr;
    const unsigned long long bytes = std::strtoull(value.c_str(), &end, 10);
    if (!end || *end != '\0') return value;

    const char *unit = "B";
    double amount = static_cast<double>(bytes);
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        amount /= 1024.0 * 1024.0 * 1024.0;
        unit = "G";
    } else if (bytes >= 1024ULL * 1024ULL) {
        amount /= 1024.0 * 1024.0;
        unit = "M";
    } else if (bytes >= 1024ULL) {
        amount /= 1024.0;
        unit = "K";
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(unit[0] == 'B' ? 0 : 1);
    out << amount << unit;
    std::string result = out.str();
    const std::string redundant_decimal = std::string(".0") + unit;
    const size_t decimal = result.rfind(redundant_decimal);
    if (decimal != std::string::npos && decimal + redundant_decimal.size() == result.size())
        result.erase(decimal, 2);
    return result;
}

std::string friendly_updated_at(std::string value)
{
    if (value.empty()) return "-";
    if (value.size() > 10 && value[10] == 'T') value[10] = ' ';
    if (value.size() > 16) value.resize(16);
    return value;
}

std::string single_line(std::string value)
{
    for (char &ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    return value;
}

} // namespace

AppStoreViewModelFactory::AppStoreViewModelFactory(AppStoreSessionState &session,
                                                   PackageJobState &package_job,
                                                   ShareCodeState &share_code)
    : session_(session), package_job_(package_job), share_code_(share_code)
{
}

CatalogDisplayViewModel AppStoreViewModelFactory::catalog(
    uint32_t now, uint32_t status_visible_ms) const
{
    CatalogDisplayViewModel model;
    model.selected_index = session_.catalog.selected_index();
    model.app_count = static_cast<int>(session_.catalog.visible().size());
    model.show_empty = session_.catalog.visible().empty();
    model.show_status = session_.status.visible(now, status_visible_ms);
    model.status = session_.status.value();
    const int visible_count = static_cast<int>(session_.catalog.visible().size());
    struct RowIndex { int row; int visible_index; };
    std::vector<RowIndex> indices;
    if (visible_count >= 5) {
        for (int row = 0; row < 5; ++row)
            indices.push_back({row, (session_.catalog.selected_index() + row - 2 + visible_count) % visible_count});
    } else if (visible_count > 0) {
        int first = std::max(0, session_.catalog.selected_index() - 2);
        int last = std::min(visible_count - 1, first + 4);
        first = std::max(0, last - 4);
        const int start_row = 2 - (session_.catalog.selected_index() - first);
        for (int index = first; index <= last; ++index) {
            const int row = start_row + index - first;
            if (row >= 0 && row < 5) indices.push_back({row, index});
        }
    }
    for (const auto &index : indices) {
        const StoreApp &app = session_.catalog.apps()[session_.catalog.visible()[index.visible_index]];
        model.rows.push_back({app.name, app.version, app.author,
                              index.visible_index == session_.catalog.selected_index(), index.row});
    }
    return model;
}

AppDetailViewModel AppStoreViewModelFactory::detail(StoreApp *selected, uint32_t now,
                                                    uint32_t status_visible_ms)
{
    AppDetailViewModel model;
    model.has_app = selected != nullptr;
    if (!selected) return model;
    model.app = *selected;
    model.app.size = human_size(selected->size);
    model.title = single_line(selected->name) + "  " + single_line(selected->version);
    model.state = selected->installed ? "Installed" : "Not installed";
    if (selected->installed && !selected->installed_version.empty())
        model.state += " " + one_line(selected->installed_version, 10);
    model.updated = friendly_updated_at(selected->updated_at);
    model.installable = can_install_app(*selected);
    model.job_running = package_job_.running;
    model.job_progress = package_job_.progress;
    model.status = session_.status.value();
    model.show_status = !package_job_.running &&
        session_.status.visible(now, status_visible_ms);
    if (model.show_status) return model;
    auto lines = DetailActionController::description_lines(*selected);
    session_.detail_media.normalize_description(selected->id, static_cast<int>(lines.size()), 3);
    const int total = static_cast<int>(lines.size());
    model.description_lines = std::move(lines);
    model.description_start = std::min(session_.detail_media.description_scroll(),
                                       std::max(0, total - 1));
    if (total > 3) {
        model.description_position = session_.detail_media.description_scroll() + 1;
        model.description_page_count = std::max(0, total - 3) + 1;
    }
    return model;
}

ConfirmationViewModel AppStoreViewModelFactory::confirmation() const
{
    return {session_.confirmation.lines(), session_.confirmation.focus()};
}

PackageProgressViewModel AppStoreViewModelFactory::progress(uint32_t now) const
{
    PackageProgressViewModel model;
    model.action = job_action_label(package_job_.action);
    model.title = package_job_.title.empty() ? "Selected app" : package_job_.title;
    model.detail = package_job_.pending_start ? "Preparing package worker" :
        (package_job_.detail.empty() ? "Waiting for package output" : package_job_.detail);
    model.progress = package_job_.progress;
    if (package_job_.progress >= 0)
        model.detail += " " + std::to_string(package_job_.progress) + "%";
    model.elapsed_seconds = package_job_.start_tick
        ? static_cast<uint32_t>(now - package_job_.start_tick) / 1000 : 0;
    return model;
}

ErrorDialogViewModel AppStoreViewModelFactory::error() const
{
    ErrorDialogViewModel model;
    model.title = session_.error.title.empty() ? "OPERATION FAILED" : session_.error.title;
    model.message = session_.error.message.empty() ? "Package operation failed." : session_.error.message;
    const std::string detail = session_.error.detail.empty()
        ? "Please try again after checking the package state." : session_.error.detail;
    model.detail_lines = wrap_display_text(detail, 44);
    model.repairable = session_.error.repairable;
    return model;
}

InitializationProgressViewModel AppStoreViewModelFactory::initialization() const
{
    InitializationProgressViewModel model;
    model.phase = upper_ascii(session_.sync.status.phase.empty() ? "registry" : session_.sync.status.phase);
    model.url = session_.sync.status.url.empty() ? session_.registry.registry_url() : session_.sync.status.url;
    model.detail = session_.sync.status.detail.empty() ? "Connecting to registry..." : session_.sync.status.detail;
    model.percent = session_.sync.status.percent;
    if (model.percent >= 0) model.detail += " " + std::to_string(model.percent) + "%";
    model.cancel_requested = session_.sync.status.cancel_requested;
    model.failed = session_.sync.startup_failed;
    model.failure_focus = session_.sync.failure_focus;
    model.error_title = session_.error.title.empty() ? "NETWORK FAILED" : session_.error.title;
    model.error_message = session_.error.message.empty() ? "Network check failed." : session_.error.message;
    const std::string detail = session_.error.detail.empty()
        ? "Check Wi-Fi before opening Store again." : session_.error.detail;
    model.error_detail_lines = wrap_display_text(detail, 44);
    return model;
}

StoreSettingsViewModel AppStoreViewModelFactory::settings(bool operation_running) const
{
    StoreSettingsViewModel model;
    model.region_code = session_.registry.region_code();
    model.active_region = session_.registry.active_region();
    model.registry_url = session_.registry.registry_url();
    model.focus = session_.registry.page_focus();
    model.loading = session_.registry_loading;
    model.selected_index = session_.registry.selected_index();
    model.entry_count = static_cast<int>(session_.registry.entries().size());
    model.has_entry = session_.registry.selected_entry() != nullptr;
    if (model.has_entry) model.entry = *session_.registry.selected_entry();
    model.operation_running = operation_running;
    model.region_commit_pending = session_.registry.region_commit_pending();
    return model;
}

RegistryEditorViewModel AppStoreViewModelFactory::registry_editor() const
{
    return {!session_.registry.edit_url().empty(), session_.registry.name_input(),
            session_.registry.input_url(), session_.registry.edit_focus()};
}

TextEntryViewModel AppStoreViewModelFactory::share_code() const
{
    return {"Share Code", "TYPE SHARE CODE", "share code", share_code_.input(),
            share_code_.message(), 0x58A6FF};
}

SearchPageViewModel AppStoreViewModelFactory::search()
{
    SearchPageViewModel model;
    model.input = session_.search.input();
    model.message = session_.search.message();
    if (!session_.search.results_active() || session_.search.results().empty()) return model;
    model.results_active = true;
    model.result_count = static_cast<int>(session_.search.results().size());
    const int total = model.result_count;
    session_.search.selected_index() = std::max(
        0, std::min(total - 1, session_.search.selected_index()));
    int first = std::max(0, session_.search.selected_index() - 1);
    int last = std::min(total - 1, first + 2);
    first = std::max(0, last - 2);
    for (int index = first; index <= last; ++index) {
        const StoreApp &app = session_.catalog.apps()[session_.search.results()[index]];
        model.rows.push_back({app.name, app.category.empty() ? app.author : app.category,
                              app.version, index == session_.search.selected_index()});
    }
    return model;
}

} // namespace appstore_ui
