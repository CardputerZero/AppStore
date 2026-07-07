#pragma once

#include <string>
#include <vector>

namespace appstore {

struct StoreApp {
    std::string id;
    std::string share_code;
    std::string name;
    std::string version;
    std::string installed_version;
    std::string category;
    bool installed = false;
    bool recommended = false;
    std::string size;
    std::string description;
    std::string author;
    std::string git_url;
    std::string images;
    std::string dependencies;
    std::string registry_name;
    std::string updated_at;
    std::string review_status;
    bool installable = false;
};

struct RegistryEntry {
    std::string url;
    std::string name;
    std::string status;
    std::string count;
    std::string updated_at;
    std::string error;
    std::string region;
    bool enabled = true;
    bool builtin = false;
};

struct SummaryData {
    std::string repo_status;
    std::string free_space;
    std::string root_path;
    std::vector<std::string> categories;
    std::vector<StoreApp> apps;
    std::string warning;
    bool saw_meta = false;
};

struct RegionData {
    std::string code = "auto";
    std::string label = "Auto";
    std::string registry_url;
    std::string active = "default";
};

struct RegistryData {
    RegionData region;
    std::vector<RegistryEntry> entries;
    std::vector<std::string> lines;
};

struct RegistryConfig {
    std::string region = "auto";
    std::string active_region = "default";
    std::vector<RegistryEntry> entries;
};

struct SyncStatus {
    bool running = false;
    bool cancel_requested = false;
    std::string url;
    std::string detail;
    int percent = -1;
    std::string phase;
    std::string updated_at;
};

enum class SortRule {
    Default,
    New,
    Old,
    AtoZ,
    ZtoA,
};

std::string tsv_unescape(const std::string &value);
std::vector<std::string> split_tab(const std::string &line);
std::string one_line(std::string value, size_t max_len);
std::string trim(std::string value);
int utf8_display_width(const std::string &text);
std::vector<std::string> wrap_display_text(std::string text, int max_width);

bool has_blocking_missing(const std::string &missing);
std::string missing_install_message(const std::string &missing);
bool can_install_app(const StoreApp &app);
bool can_reinstall_app(const StoreApp &app);
bool can_upgrade_app(const StoreApp &app);
std::string review_label(const StoreApp &app);

std::string job_action_label(const std::string &action);
std::string backend_error_message(const std::string &out);
std::string sync_status_message(const std::string &out);

std::string upper_ascii(std::string value);
std::string match_key(std::string value);
std::string first_csv(std::string value);
std::vector<std::string> split_csv_paths(const std::string &value);
std::string sort_rule_label(SortRule rule);
void sort_apps(std::vector<StoreApp> &apps, SortRule rule);

void set_backend_script_path(std::string path);
const std::string &backend_script_path();
bool start_backend_service();
void stop_backend_service();
std::string backend_capture(const std::vector<std::string> &args, int *rc = nullptr);
std::string backend_capture_with_sudo(const std::vector<std::string> &args,
                                      const std::string &password,
                                      int *rc = nullptr);
SyncStatus load_sync_status();
bool cancel_sync();
RegistryConfig load_registry_config();
bool replace_registry_config(const RegistryConfig &config);
SummaryData load_summary(SortRule rule);
RegistryData load_registries(const std::string &fallback_registry_url);

}  // namespace appstore
