#pragma once

#include "appstore_client.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace appstore_ui {

struct InitializationProgressViewModel {
    std::string phase;
    std::string url;
    std::string detail;
    int percent = -1;
    bool cancel_requested = false;
    bool failed = false;
    int failure_focus = 0;
    std::string error_title;
    std::string error_message;
    std::vector<std::string> error_detail_lines;
};

struct CatalogRowViewModel {
    std::string name;
    std::string version;
    std::string author;
    bool selected = false;
    int row = 0;
};

struct CatalogDisplayViewModel {
    std::vector<CatalogRowViewModel> rows;
    int selected_index = 0;
    int app_count = 0;
    bool show_empty = false;
    bool show_status = false;
    std::string status;
};

struct TextEntryViewModel {
    std::string title;
    std::string prompt;
    std::string placeholder;
    std::string input;
    std::string message;
    uint32_t accent = 0x58A6FF;
    int input_x = 68;
    int input_width = 184;
    size_t display_limit = 12;
};

struct SearchResultRowViewModel {
    std::string name;
    std::string meta;
    std::string version;
    bool selected = false;
};

struct SearchPageViewModel {
    std::string input;
    std::string message;
    bool results_active = false;
    int result_count = 0;
    std::vector<SearchResultRowViewModel> rows;
};

struct AppDetailViewModel {
    bool has_app = false;
    appstore::StoreApp app;
    std::string title;
    std::string state;
    std::string review;
    bool installable = false;
    bool show_status = false;
    std::string status;
    std::vector<std::string> description_lines;
    int description_position = 0;
    int description_page_count = 0;
    bool job_running = false;
    int job_progress = -1;
};

struct ConfirmationViewModel {
    std::vector<std::string> lines;
    int focus = 0;
};

struct PackageProgressViewModel {
    std::string action;
    std::string title;
    std::string detail;
    int progress = -1;
    uint32_t elapsed_seconds = 0;
};

struct ErrorDialogViewModel {
    std::string title;
    std::string message;
    std::vector<std::string> detail_lines;
    bool repairable = false;
};

struct ScreenshotOverlayViewModel {
    bool has_app = false;
    bool has_screenshots = false;
    bool overlay_visible = false;
    int image_index = 0;
    int image_count = 0;
    bool loading = false;
    bool load_failed = false;
};

struct StoreSettingsViewModel {
    std::string region_code;
    std::string active_region;
    std::string registry_url;
    int focus = 0;
    bool loading = false;
    bool has_entry = false;
    appstore::RegistryEntry entry;
    int selected_index = 0;
    int entry_count = 0;
    bool operation_running = false;
    bool region_commit_pending = false;
};

struct RegistryEditorViewModel {
    bool editing = false;
    std::string name;
    std::string url;
    int focus = 0;
};

} // namespace appstore_ui
