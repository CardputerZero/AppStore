#include "appstore_view_model_factory.hpp"
#include "detail_action_controller.hpp"

#include <cassert>
#include <iostream>

namespace appstore {

std::string one_line(std::string value, size_t max_len)
{
    if (value.size() > max_len) value.resize(max_len);
    return value;
}

std::vector<std::string> wrap_display_text(std::string text, int)
{
    return {std::move(text)};
}

bool can_install_app(const StoreApp &app) { return app.installable; }
std::string review_label(const StoreApp &app) { return app.review_status; }
std::string job_action_label(const std::string &action)
{
    return action == "install" ? "Installing" : action;
}

std::string upper_ascii(std::string value)
{
    for (char &ch : value)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    return value;
}

std::string match_key(std::string value)
{
    for (char &ch : value)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return value;
}

} // namespace appstore

namespace appstore_ui {

std::vector<std::string> DetailActionController::description_lines(
    const appstore::StoreApp &app)
{
    return appstore::wrap_display_text(app.description, 38);
}

} // namespace appstore_ui

int main()
{
    using namespace appstore_ui;
    AppStoreSessionState session;
    PackageJobState package_job;
    ShareCodeState share_code;
    AppStoreViewModelFactory factory(session, package_job, share_code);

    session.catalog.apps().resize(7);
    for (int i = 0; i < 7; ++i) {
        session.catalog.apps()[i].name = "App " + std::to_string(i);
        session.catalog.visible().push_back(i);
    }
    session.catalog.selected_index() = 0;
    auto catalog = factory.catalog(100, 6000);
    assert(catalog.rows.size() == 5);
    assert(catalog.rows[0].name == "App 5" && catalog.rows[0].row == 0);
    assert(catalog.rows[2].name == "App 0" && catalog.rows[2].selected);

    auto &app = session.catalog.apps()[0];
    app.version = "2.0";
    app.installed = true;
    app.installed_version = "1.0";
    app.installable = true;
    app.review_status = "Approved";
    app.description = "Description";
    app.size = "20560446";
    app.updated_at = "2026-07-23T14:32:18+08:00";
    app.name = "A very long application name that must stay intact";
    auto detail = factory.detail(&app, 100, 6000);
    assert(detail.state == "Installed 1.0");
    assert(detail.installable && detail.description_lines.size() == 1);
    assert(detail.app.size == "19.6M");
    assert(detail.updated == "2026-07-23 14:32");
    assert(detail.title == "A very long application name that must stay intact  2.0");

    app.name = "Line one\nLine two";
    app.version = "2.0\tbeta";
    detail = factory.detail(&app, 100, 6000);
    assert(detail.title == "Line one Line two  2.0 beta");
    app.name = "App 0";
    app.version = "2.0";

    session.status.value() = "Refreshing";
    detail = factory.detail(&app, 101, 6000);
    assert(detail.show_status && detail.description_lines.empty());

    package_job.running = true;
    package_job.action = "install";
    package_job.title = "Demo";
    package_job.detail = "Downloading";
    package_job.progress = 25;
    package_job.start_tick = 0xFFFFFF00U;
    auto progress = factory.progress(0x000007D0U);
    assert(progress.action == "Installing");
    assert(progress.detail == "Downloading 25%");
    assert(progress.elapsed_seconds == 2);

    auto error = factory.error();
    assert(error.title == "OPERATION FAILED");
    assert(error.message == "Package operation failed.");
    auto initialization = factory.initialization();
    assert(initialization.phase == "REGISTRY");
    assert(initialization.detail == "Connecting to registry...");

    session.registry.entries().push_back(
        {"https://example.test/registry.json", "Example", "ok", "1", "now", "", "global", true, false});
    auto settings = factory.settings(false);
    assert(settings.has_entry && settings.entry.name == "Example");

    share_code.input() = "ABC123";
    auto share = factory.share_code();
    assert(share.title == "Share Code" && share.input == "ABC123");
    assert(share.accent == 0x58A6FF);

    session.search.input() = "App";
    assert(!session.search.submit(session.catalog.apps()).has_value());
    assert(session.search.results_active());
    session.search.selected_index() = 6;
    auto search = factory.search();
    assert(search.result_count == 7 && search.rows.size() == 3);
    assert(search.rows.back().selected && search.rows.back().name == "App 6");

    std::cout << "appstore view model factory tests passed\n";
}
