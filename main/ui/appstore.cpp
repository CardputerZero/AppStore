#include "lvgl/lvgl.h"
#include "main.h"

#include <stdint.h>
#include "appstore_fonts.hpp"
#include "appstore_paths.hpp"
#include "appstore_business.hpp"
#include "keyboard_input.h"
#include "compat/input_keys.h"
#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <pthread.h>
#include <sys/stat.h>

void cp0_zmq_log_init(void);
void cp0_zmq_log(const char *topic, const char *message);

namespace {

using namespace appstore;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kHeaderHeight = 24;
constexpr int kLineHeight = 15;
constexpr int kHomeIconX = 10;
constexpr int kHomeIconY = 45;
constexpr int kHomeIconSize = 68;
constexpr int kShortcutCenterY = 153;
constexpr int kShortcutSpecScreenLeft = 132;
constexpr int kShortcutIconSize = 16;
constexpr int kShortcutTextWidth = 54;
constexpr uint32_t kEscLongPressMs = 1200;
constexpr uint32_t kJobStartDelayMs = 80;
constexpr uint32_t kJobPollIntervalMs = 250;
constexpr uint32_t kTopStatusRefreshMs = 5000;
constexpr uint32_t kBatteryChargeAnimRefreshMs = 120;
constexpr uint32_t kSyncAnimRefreshMs = 200;
constexpr uint32_t kRegionDebounceMs = 2000;
constexpr uint32_t kStatusScrollVisibleMs = 6000;
constexpr int kSharedRegistryMaxEntries = 16;
constexpr const char *kDefaultRegistryUrl = "https://cardputerzero.github.io/generated/registry.json";
constexpr const char *kSharedRegistryPrefix = "appstore.registry.";

enum class Screen {
    StartupSync,
    Home,
    Detail,
    Confirm,
    SudoPassword,
    Progress,
    ErrorDialog,
    Registry,
    RegistryEdit,
    ShareCode,
    Search,
    Screenshots,
};

enum class RegistryOpKind {
    None,
    SetRegion,
    AddRegistry,
    EditRegistry,
    ToggleRegistry,
    DeleteRegistry,
};

struct KeyEvent {
    uint32_t code = 0;
    uint32_t mods = 0;
    char ch = 0;
    bool release = false;
    bool repeated = false;
};

struct RegistryOpRequest {
    RegistryOpKind kind = RegistryOpKind::None;
    uint64_t generation = 0;
    std::string region;
    std::string url;
    std::string old_url;
    std::string name;
    bool enable = false;
};

struct RegistryOpResult {
    RegistryOpRequest request;
    std::string output;
    int rc = -1;
};

struct RegistryRefreshRequest {
    std::string fallback;
    uint64_t generation = 0;
};

struct SyncRequest {
    uint64_t generation = 0;
};

struct SummaryRequest {
    SortRule rule = SortRule::Default;
    uint64_t generation = 0;
};

lv_obj_t *g_root = nullptr;
lv_group_t *g_group = nullptr;
lv_timer_t *g_refresh_timer = nullptr;
lv_timer_t *g_esc_hold_timer = nullptr;
lv_timer_t *g_job_timer = nullptr;
lv_timer_t *g_sync_timer = nullptr;
volatile sig_atomic_t g_quit_requested = 0;

std::string g_app_dir = ".";
std::string g_status_message;
std::string g_status_scroll_text;
uint32_t g_status_scroll_start_tick = 0;
uint32_t g_status_scroll_hide_after_ms = 0;
std::string g_repo_status = "built-in";
std::string g_free_space = "-";
std::string g_root_path = "-";
std::vector<std::string> g_categories = {"Recommended", "All"};
std::vector<StoreApp> g_apps;
std::vector<int> g_visible;
std::vector<std::string> g_render_image_sources;
std::vector<std::string> g_registry_lines;
std::vector<RegistryEntry> g_registry_entries;
int g_category = 0;
int g_selected = 0;
int g_registry_selected = 0;
int g_registry_page_focus = 0;
Screen g_screen = Screen::Home;
bool g_default_category_applied = false;
SortRule g_sort_rule = SortRule::Default;
std::string g_confirm_action;
std::vector<std::string> g_confirm_lines;
int g_confirm_focus = 0;
std::string g_sudo_password_input;
std::string g_sudo_message = "Enter sudo password for package operation.";
std::string g_sudo_action;
std::string g_sudo_app_id;
std::string g_sudo_app_title;
std::string g_error_title;
std::string g_error_message;
std::string g_error_detail;
std::string g_registry_input = kDefaultRegistryUrl;
std::string g_registry_edit_url;
std::string g_registry_name_input;
int g_registry_focus = 0;
std::string g_region_code = "auto";
std::string g_region_label = "Auto";
std::string g_region_registry_url = kDefaultRegistryUrl;
std::string g_region_active = "default";
std::string g_share_code_input;
std::string g_share_code_message = "Enter a code from CardputerZero Hub.";
std::string g_search_input;
std::string g_search_message = "Search by app name, author, category, or code.";
std::vector<int> g_search_results;
int g_search_selected = 0;
bool g_search_results_active = false;
std::string g_detail_media_app_id;
int g_detail_image_index = 0;
std::string g_detail_description_app_id;
int g_detail_description_scroll = 0;
uint32_t g_screenshots_activity_tick = 0;
bool g_screenshots_overlay_visible = true;
bool g_job_running = false;
bool g_job_pending_start = false;
std::string g_job_action;
std::string g_job_app_id;
std::string g_job_title;
std::string g_job_stage;
std::string g_job_detail;
std::string g_job_output;
std::string g_job_sudo_password;
int g_job_progress = -1;
int g_job_rc = -1;
pthread_t g_job_thread = {};
uint32_t g_job_start_tick = 0;
bool g_job_done = false;
uint32_t g_share_code_open_tick = 0;
uint32_t g_esc_press_tick = 0;
bool g_esc_pressed = false;
bool g_esc_long_consumed = false;
pthread_mutex_t g_job_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_sync_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_sync_running = false;
bool g_sync_done = false;
bool g_sync_refresh_registries = false;
std::string g_sync_output;
SyncStatus g_sync_status;
uint32_t g_sync_visible_until_tick = 0;
uint64_t g_sync_generation = 0;
bool g_startup_sync_active = false;
bool g_startup_sync_cancelled = false;
bool g_startup_sync_failed = false;
pthread_mutex_t g_summary_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_summary_running = false;
bool g_summary_done = false;
bool g_summary_pending = false;
SortRule g_summary_rule = SortRule::Default;
SummaryData g_summary_result;
uint64_t g_summary_generation = 0;
pthread_mutex_t g_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_registry_refresh_running = false;
bool g_registry_refresh_done = false;
bool g_registry_refresh_pending = false;
bool g_registry_loading = false;
RegistryData g_registry_result;
uint64_t g_registry_refresh_generation = 0;
pthread_mutex_t g_registry_op_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_registry_op_running = false;
bool g_registry_op_done = false;
RegistryOpResult g_registry_op_result;
RegistryOpKind g_registry_op_kind = RegistryOpKind::None;
uint64_t g_registry_op_generation = 0;
pthread_mutex_t g_plan_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_plan_running = false;
bool g_plan_done = false;
std::string g_plan_action;
std::string g_plan_app_id;
std::string g_plan_output;
int g_plan_rc = -1;
cp0_wifi_status_t g_top_wifi_status = {};
cp0_battery_info_t g_top_battery_status = {};
uint32_t g_top_status_tick = 0;
uint32_t g_top_status_last_render_tick = 0;
int g_sync_anim_phase = -1;
bool g_region_commit_pending = false;
std::string g_region_pending_code;
uint32_t g_region_change_tick = 0;

const char *screen_name(Screen screen)
{
    switch (screen) {
        case Screen::StartupSync: return "StartupSync";
        case Screen::Home: return "Home";
        case Screen::Detail: return "Detail";
        case Screen::Confirm: return "Confirm";
        case Screen::SudoPassword: return "SudoPassword";
        case Screen::Progress: return "Progress";
        case Screen::ErrorDialog: return "ErrorDialog";
        case Screen::Registry: return "Registry";
        case Screen::RegistryEdit: return "RegistryEdit";
        case Screen::ShareCode: return "ShareCode";
        case Screen::Search: return "Search";
        case Screen::Screenshots: return "Screenshots";
    }
    return "Unknown";
}

void app_tracef(const char *fmt, ...)
{
    char buf[1024] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "[AppStore TRACE] %s\n", buf);
    cp0_zmq_log("appstore", buf);
}

std::string probe_file_for_log(const std::string &path)
{
    struct stat st {};
    if (path.empty()) return "empty-path";
    if (stat(path.c_str(), &st) != 0) return "missing";
    if (S_ISDIR(st.st_mode)) return "dir";
    return "file size=" + std::to_string(static_cast<long long>(st.st_size));
}

std::string config_key(const std::string &suffix)
{
    return std::string(kSharedRegistryPrefix) + suffix;
}

std::string shared_config_get(const std::string &key, const std::string &fallback = "")
{
    std::string value = fallback;
    cp0_signal_config_api({"GetStr", key, fallback}, [&](int code, std::string data) {
        if (code == 0) value = std::move(data);
    });
    return value;
}

void shared_config_set(const std::string &key, const std::string &value)
{
    cp0_signal_config_api({"SetStr", key, value}, nullptr);
}

void shared_config_save()
{
    cp0_signal_config_api({"Save"}, nullptr);
}

std::string pack_field_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::vector<std::string> split_packed_entry(const std::string &value)
{
    std::vector<std::string> out;
    std::string cur;
    bool escaped = false;
    for (char ch : value) {
        if (escaped) {
            if (ch == 't') cur += '\t';
            else if (ch == 'n') cur += '\n';
            else if (ch == 'r') cur += '\r';
            else cur += ch;
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '\t') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (escaped) cur += '\\';
    out.push_back(cur);
    return out;
}

std::string pack_registry_entry(const RegistryEntry &entry)
{
    return pack_field_escape(entry.name) + "\t" +
        pack_field_escape(entry.url) + "\t" +
        (entry.enabled ? "1" : "0") + "\t" +
        (entry.builtin ? "1" : "0") + "\t" +
        pack_field_escape(entry.region);
}

bool unpack_registry_entry(const std::string &packed, RegistryEntry &entry)
{
    auto fields = split_packed_entry(packed);
    if (fields.size() < 5 || fields[1].empty()) return false;
    entry.name = fields[0];
    entry.url = fields[1];
    entry.enabled = fields[2] != "0";
    entry.builtin = fields[3] == "1";
    entry.region = fields[4];
    return true;
}

bool load_shared_registry_config(RegistryConfig &config)
{
    std::string count_text = shared_config_get(config_key("count"), "");
    if (count_text.empty()) return false;
    int count = std::atoi(count_text.c_str());
    if (count <= 0) return false;
    count = std::min(count, kSharedRegistryMaxEntries);

    config = RegistryConfig{};
    config.region = shared_config_get(config_key("region"), "auto");
    config.active_region = shared_config_get(config_key("active_region"), "default");
    for (int i = 0; i < count; ++i) {
        RegistryEntry entry;
        std::string packed = shared_config_get(config_key(std::to_string(i) + ".entry"), "");
        if (unpack_registry_entry(packed, entry)) {
            config.entries.push_back(entry);
        }
    }
    return !config.entries.empty();
}

bool save_shared_registry_config(const RegistryConfig &config)
{
    int count = std::min(static_cast<int>(config.entries.size()), kSharedRegistryMaxEntries);
    shared_config_set(config_key("region"), config.region.empty() ? "auto" : config.region);
    shared_config_set(config_key("active_region"), config.active_region.empty() ? "default" : config.active_region);
    shared_config_set(config_key("count"), std::to_string(count));
    for (int i = 0; i < kSharedRegistryMaxEntries; ++i) {
        std::string key = config_key(std::to_string(i) + ".entry");
        if (i < count) shared_config_set(key, pack_registry_entry(config.entries[i]));
        else shared_config_set(key, "");
    }
    shared_config_save();
    app_tracef("shared_registry save region=%s active=%s entries=%d",
               config.region.c_str(), config.active_region.c_str(), count);
    return true;
}

void apply_shared_registry_hint(const RegistryConfig &config)
{
    g_region_code = config.region.empty() ? "auto" : config.region;
    g_region_active = config.active_region.empty() ? "default" : config.active_region;
    for (const RegistryEntry &entry : config.entries) {
        if (entry.builtin || entry.region == g_region_active) {
            g_region_registry_url = entry.url;
            return;
        }
    }
    if (!config.entries.empty()) {
        g_region_registry_url = config.entries.front().url;
    }
}

void sync_shared_registry_config_on_startup()
{
    RegistryConfig shared;
    if (load_shared_registry_config(shared)) {
        app_tracef("shared_registry startup import entries=%zu region=%s active=%s",
                   shared.entries.size(), shared.region.c_str(), shared.active_region.c_str());
        apply_shared_registry_hint(shared);
        if (!replace_registry_config(shared)) {
            g_status_message = "Unable to apply saved registry settings";
        }
        return;
    }

    RegistryConfig backend = load_registry_config();
    if (!backend.entries.empty()) {
        save_shared_registry_config(backend);
        apply_shared_registry_hint(backend);
        app_tracef("shared_registry migrated entries=%zu region=%s active=%s",
                   backend.entries.size(), backend.region.c_str(), backend.active_region.c_str());
    }
}

void save_backend_registry_config_to_shared()
{
    RegistryConfig backend = load_registry_config();
    if (backend.entries.empty()) {
        app_tracef("shared_registry save skipped: backend config empty");
        return;
    }
    save_shared_registry_config(backend);
}

struct TraceScope {
    const char *name;
    uint32_t start;
    bool always;

    TraceScope(const char *scope_name, bool log_always = false)
        : name(scope_name), start(lv_tick_get()), always(log_always)
    {
        if (always) app_tracef("%s begin screen=%s", name, screen_name(g_screen));
    }

    ~TraceScope()
    {
        uint32_t elapsed = lv_tick_elaps(start);
        if (always || elapsed >= 20) {
            app_tracef("%s end elapsed=%ums screen=%s", name, elapsed, screen_name(g_screen));
        }
    }
};

void request_quit()
{
    g_quit_requested = 1;
    lvgl_wake();
}

void handle_signal(int)
{
    request_quit();
}

bool key_matches(const KeyEvent &key, char ch, uint32_t code)
{
    return key.ch == ch || key.code == code;
}

void parse_job_progress(const std::string &out)
{
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.size() >= 6 && fields[0] == "PROGRESS") {
            g_job_stage = fields[1];
            g_job_progress = std::atoi(fields[4].c_str());
            g_job_detail = fields[5];
        }
    }
}

std::string current_category_name()
{
    if (g_categories.empty()) return "All";
    if (g_category < 0 || g_category >= static_cast<int>(g_categories.size())) return "All";
    return g_categories[g_category];
}

bool select_category_by_name(const std::string &name)
{
    for (int i = 0; i < static_cast<int>(g_categories.size()); ++i) {
        if (g_categories[i] == name) {
            g_category = i;
            return true;
        }
    }
    return false;
}

void select_default_category()
{
    if (!select_category_by_name("All") && !g_categories.empty()) {
        g_category = 0;
    }
    std::fprintf(stderr, "[AppStore UI] select_default_category index=%d name=%s cats=%zu\n",
                 g_category, current_category_name().c_str(), g_categories.size());
}

std::vector<std::string> detail_description_lines(const StoreApp &app)
{
    return wrap_display_text(app.description.empty() ? "-" : app.description, 48);
}

void rebuild_visible()
{
    g_visible.clear();
    std::string cat = current_category_name();
    int recommended_count = 0;
    int exact_category_count = 0;
    for (int i = 0; i < static_cast<int>(g_apps.size()); ++i) {
        if (g_apps[i].recommended) ++recommended_count;
        if (g_apps[i].category == cat) ++exact_category_count;
        bool show = cat == "All" ||
            (cat == "Recommended" && g_apps[i].recommended) ||
            g_apps[i].category == cat;
        if (show) g_visible.push_back(i);
    }
    if (g_selected >= static_cast<int>(g_visible.size())) g_selected = static_cast<int>(g_visible.size()) - 1;
    if (g_selected < 0) g_selected = 0;
    std::fprintf(stderr,
                 "[AppStore UI] rebuild_visible category=%s category_index=%d apps=%zu visible=%zu recommended=%d exact_category=%d selected=%d\n",
                 cat.c_str(), g_category, g_apps.size(), g_visible.size(), recommended_count,
                 exact_category_count, g_selected);
}

StoreApp *selected_app()
{
    if (g_visible.empty() || g_selected < 0 || g_selected >= static_cast<int>(g_visible.size())) return nullptr;
    return &g_apps[g_visible[g_selected]];
}

std::vector<std::string> split_colon(const std::string &line)
{
    std::vector<std::string> cols;
    std::string current;
    for (char ch : line) {
        if (ch == ':') {
            cols.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    cols.push_back(current);
    return cols;
}

void copy_string(char *dst, size_t dst_size, const std::string &src)
{
    if (!dst || dst_size == 0) return;
    std::snprintf(dst, dst_size, "%s", src.c_str());
}

cp0_wifi_status_t get_wifi_status()
{
    cp0_wifi_status_t st{};
    cp0_signal_wifi_api({"Status"}, [&](int code, std::string data) {
        if (code != 0) return;
        auto cols = split_colon(data);
        if (cols.size() < 4) return;
        st.connected = std::atoi(cols[0].c_str());
        copy_string(st.ssid, sizeof(st.ssid), cols[1]);
        copy_string(st.ip, sizeof(st.ip), cols[2]);
        st.signal = std::atoi(cols[3].c_str());
        if (cols.size() >= 5) st.ethernet = std::atoi(cols[4].c_str());
    });
    return st;
}

void update_top_status_cache(bool force = false)
{
    if (!force && g_top_status_tick != 0 && lv_tick_elaps(g_top_status_tick) < kTopStatusRefreshMs) {
        return;
    }
    uint32_t start = lv_tick_get();
    uint32_t wifi_start = start;
    g_top_wifi_status = get_wifi_status();
    uint32_t wifi_ms = lv_tick_elaps(wifi_start);
    uint32_t battery_start = lv_tick_get();
    g_top_battery_status = cp0_battery_read();
    uint32_t battery_ms = lv_tick_elaps(battery_start);
    g_top_status_tick = lv_tick_get();
    uint32_t total_ms = lv_tick_elaps(start);
    if (force || total_ms >= 10 || g_screen == Screen::Registry || g_screen == Screen::RegistryEdit) {
        app_tracef("top_status update force=%d total=%ums wifi=%ums battery=%ums screen=%s wifi_connected=%d battery_valid=%d flags=%d",
                   force ? 1 : 0, total_ms, wifi_ms, battery_ms, screen_name(g_screen),
                   g_top_wifi_status.connected, g_top_battery_status.valid,
                   g_top_battery_status.flags);
    }
}

void request_summary_refresh();
void sync_catalog(bool refresh_registries_after = false);
void cancel_startup_sync_and_open_registry();
void open_registry_screen();
bool poll_plan_check();
void start_plan_check(const std::string &action, const std::string &app_id);
bool poll_region_debounce();
void update_local_job_app_state(bool ok, const std::string &out);

StoreApp *ensure_selected_app()
{
    StoreApp *app = selected_app();
    if (app) return app;

    if (!g_apps.empty()) {
        select_default_category();
        rebuild_visible();
        g_selected = 0;
        app = selected_app();
        if (app) return app;
    }

    request_summary_refresh();
    return nullptr;
}

bool open_detail_screen()
{
    if (ensure_selected_app()) {
        g_screen = Screen::Detail;
        return true;
    }
    g_status_message = g_apps.empty() ? "Catalog is still loading" : "No app selected";
    g_screen = Screen::Home;
    return false;
}

bool select_visible_app_by_id(const std::string &app_id)
{
    if (app_id.empty()) return false;
    for (int i = 0; i < static_cast<int>(g_visible.size()); ++i) {
        if (g_apps[g_visible[i]].id == app_id) {
            g_selected = i;
            return true;
        }
    }
    return false;
}

void apply_summary(const SummaryData &summary)
{
    std::fprintf(stderr,
                 "[AppStore UI] apply_summary saw_meta=%d apps=%zu cats=%zu status=%s warning=%s\n",
                 summary.saw_meta ? 1 : 0, summary.apps.size(), summary.categories.size(),
                 summary.repo_status.c_str(), summary.warning.c_str());
    StoreApp *previous_app = selected_app();
    std::string previous_app_id = previous_app ? previous_app->id : "";
    std::string previous_category = current_category_name();

    if (!summary.categories.empty()) g_categories = summary.categories;
    if (summary.saw_meta) {
        g_repo_status = summary.repo_status;
        g_free_space = summary.free_space;
        g_root_path = summary.root_path;
        g_apps = summary.apps;
        sort_apps(g_apps, g_sort_rule);
    }
    if (!g_default_category_applied) {
        select_default_category();
        g_default_category_applied = true;
        g_selected = 0;
    } else if (!previous_category.empty() && select_category_by_name(previous_category)) {
        // Keep the user's current filter stable across periodic refreshes.
    } else if (g_category >= static_cast<int>(g_categories.size())) {
        select_default_category();
    }
    rebuild_visible();
    select_visible_app_by_id(previous_app_id);
    StoreApp *selected_after = selected_app();
    if (selected_after) {
        app_tracef("summary selected id=%s name=%s installed=%d installed_version=%s images=%s",
                   selected_after->id.c_str(), one_line(selected_after->name, 40).c_str(),
                   selected_after->installed ? 1 : 0,
                   selected_after->installed_version.empty() ? "-" : selected_after->installed_version.c_str(),
                   selected_after->images.empty() ? "-" : one_line(selected_after->images, 120).c_str());
    }
    std::fprintf(stderr,
                 "[AppStore UI] apply_summary result apps=%zu visible=%zu category=%s selected=%d previous_app=%s status=%s message=%s\n",
                 g_apps.size(), g_visible.size(), current_category_name().c_str(), g_selected,
                 previous_app_id.empty() ? "-" : previous_app_id.c_str(), g_repo_status.c_str(),
                 g_status_message.empty() ? "-" : g_status_message.c_str());
    if (!summary.warning.empty()) {
        g_status_message = one_line(summary.warning, 54);
    } else if (g_status_message.rfind("Registry offline", 0) == 0 ||
               g_status_message.rfind("Unable to load", 0) == 0) {
        g_status_message.clear();
    }
}

void apply_registry_data(const RegistryData &registries)
{
    g_region_code = registries.region.code;
    g_region_label = registries.region.label;
    g_region_registry_url = registries.region.registry_url;
    g_region_active = registries.region.active;
    g_registry_entries = registries.entries;
    g_registry_lines = registries.lines;
    if (g_registry_selected >= static_cast<int>(g_registry_entries.size())) {
        g_registry_selected = std::max(0, static_cast<int>(g_registry_entries.size()) - 1);
    }
    if (g_registry_selected < 0) g_registry_selected = 0;
}

void *registry_refresh_thread_main(void *arg)
{
    RegistryRefreshRequest request = *static_cast<RegistryRefreshRequest *>(arg);
    delete static_cast<RegistryRefreshRequest *>(arg);
    uint32_t start = lv_tick_get();
    app_tracef("registry_refresh worker begin gen=%llu fallback=%s",
               static_cast<unsigned long long>(request.generation),
               request.fallback.empty() ? "-" : request.fallback.c_str());
    RegistryData registries = load_registries(request.fallback);
    uint32_t elapsed = lv_tick_elaps(start);
    app_tracef("registry_refresh worker end gen=%llu elapsed=%ums entries=%zu region=%s",
               static_cast<unsigned long long>(request.generation), elapsed,
               registries.entries.size(), registries.region.code.c_str());
    pthread_mutex_lock(&g_registry_mutex);
    if (request.generation == g_registry_refresh_generation) {
        g_registry_result = std::move(registries);
        g_registry_refresh_done = true;
    } else {
        app_tracef("registry_refresh worker stale gen=%llu current=%llu",
                   static_cast<unsigned long long>(request.generation),
                   static_cast<unsigned long long>(g_registry_refresh_generation));
    }
    pthread_mutex_unlock(&g_registry_mutex);
    return nullptr;
}

void request_registry_refresh()
{
    std::fprintf(stderr, "[AppStore UI] request_registry_refresh\n");
    app_tracef("registry_refresh request screen=%s loading=%d entries=%zu",
               screen_name(g_screen), g_registry_loading ? 1 : 0, g_registry_entries.size());
    pthread_mutex_lock(&g_registry_mutex);
    if (g_registry_refresh_running) {
        g_registry_refresh_pending = true;
        g_registry_loading = true;
        pthread_mutex_unlock(&g_registry_mutex);
        std::fprintf(stderr, "[AppStore UI] request_registry_refresh pending: already running\n");
        return;
    }
    g_registry_refresh_running = true;
    g_registry_refresh_done = false;
    g_registry_loading = true;
    uint64_t generation = ++g_registry_refresh_generation;
    std::string fallback = g_region_registry_url;
    pthread_mutex_unlock(&g_registry_mutex);

    pthread_t thread_id;
    auto *thread_arg = new RegistryRefreshRequest{fallback, generation};
    if (pthread_create(&thread_id, nullptr, registry_refresh_thread_main, thread_arg) != 0) {
        delete thread_arg;
        pthread_mutex_lock(&g_registry_mutex);
        g_registry_refresh_running = false;
        g_registry_loading = false;
        pthread_mutex_unlock(&g_registry_mutex);
        g_status_message = "Unable to refresh registries";
        std::fprintf(stderr, "[AppStore UI] request_registry_refresh failed: pthread_create\n");
        return;
    }
    pthread_detach(thread_id);
}

bool poll_registry_refresh()
{
    uint32_t start = lv_tick_get();
    RegistryData registries;
    bool done = false;
    bool start_next = false;

    pthread_mutex_lock(&g_registry_mutex);
    if (g_registry_refresh_done) {
        done = true;
        registries = std::move(g_registry_result);
        g_registry_refresh_done = false;
        g_registry_refresh_running = false;
        g_registry_loading = false;
        start_next = g_registry_refresh_pending;
        g_registry_refresh_pending = false;
    }
    pthread_mutex_unlock(&g_registry_mutex);

    if (!done) return false;

    std::fprintf(stderr, "[AppStore UI] poll_registry_refresh done start_next=%d entries=%zu region=%s\n",
                 start_next ? 1 : 0, registries.entries.size(), registries.region.code.c_str());
    apply_registry_data(registries);
    app_tracef("registry_refresh poll apply elapsed=%ums start_next=%d entries=%zu region=%s selected=%d",
               lv_tick_elaps(start), start_next ? 1 : 0, g_registry_entries.size(),
               g_region_code.c_str(), g_registry_selected);
    if (start_next) request_registry_refresh();
    return true;
}

std::string registry_op_busy_message()
{
    return "Registry operation running";
}

bool registry_op_is_running()
{
    pthread_mutex_lock(&g_registry_op_mutex);
    bool running = g_registry_op_running;
    pthread_mutex_unlock(&g_registry_op_mutex);
    return running;
}

bool registry_refresh_is_running()
{
    pthread_mutex_lock(&g_registry_mutex);
    bool running = g_registry_refresh_running || g_registry_loading;
    pthread_mutex_unlock(&g_registry_mutex);
    return running;
}

bool sync_output_failed_for_startup(const std::string &out, std::string *message)
{
    std::string fallback;
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.empty()) continue;
        if (fields[0] == "ERROR") {
            if (fields.size() >= 2 && !fields[1].empty()) {
                if (message) *message = fields[1];
            } else if (message) {
                *message = "Network connection failed";
            }
            return true;
        }
        if (fields[0] == "SYNC" && fields.size() >= 6) {
            int cached = std::atoi(fields[3].c_str());
            int failed = std::atoi(fields[4].c_str());
            if (!fields[5].empty()) fallback = fields[5];
            if (cached > 0 || failed > 0) {
                if (message) {
                    *message = !fields[5].empty() ? fields[5] : "Network connection failed";
                }
                return true;
            }
        }
    }
    if (message) *message = fallback.empty() ? "Network connection failed" : fallback;
    return false;
}

bool sync_is_running()
{
    pthread_mutex_lock(&g_sync_mutex);
    bool running = g_sync_running;
    pthread_mutex_unlock(&g_sync_mutex);
    return running;
}

void cancel_registry_online_work(const char *reason)
{
    app_tracef("registry_online cancel reason=%s screen=%s",
               reason ? reason : "-", screen_name(g_screen));

    pthread_mutex_lock(&g_registry_mutex);
    ++g_registry_refresh_generation;
    g_registry_refresh_running = false;
    g_registry_refresh_done = false;
    g_registry_refresh_pending = false;
    g_registry_loading = false;
    pthread_mutex_unlock(&g_registry_mutex);

    pthread_mutex_lock(&g_sync_mutex);
    ++g_sync_generation;
    g_sync_running = false;
    g_sync_done = false;
    g_sync_refresh_registries = false;
    g_sync_output.clear();
    g_sync_anim_phase = -1;
    pthread_mutex_unlock(&g_sync_mutex);

    pthread_mutex_lock(&g_summary_mutex);
    ++g_summary_generation;
    g_summary_running = false;
    g_summary_done = false;
    g_summary_pending = false;
    pthread_mutex_unlock(&g_summary_mutex);

    pthread_mutex_lock(&g_registry_op_mutex);
    if (g_registry_op_running && g_registry_op_kind == RegistryOpKind::SetRegion) {
        ++g_registry_op_generation;
        g_registry_op_running = false;
        g_registry_op_done = false;
        g_registry_op_kind = RegistryOpKind::None;
    }
    pthread_mutex_unlock(&g_registry_op_mutex);

    if (!g_job_running && !g_job_pending_start) {
        stop_backend_service();
    }
}

bool registry_op_available()
{
    if (registry_op_is_running()) {
        g_status_message = registry_op_busy_message();
        app_tracef("registry_op unavailable reason=registry_op screen=%s", screen_name(g_screen));
        return false;
    }
    if (registry_refresh_is_running()) {
        g_status_message = "Loading registries...";
        app_tracef("registry_op unavailable reason=registry_refresh screen=%s", screen_name(g_screen));
        return false;
    }
    if (sync_is_running()) {
        g_status_message = "Syncing catalog...";
        app_tracef("registry_op unavailable reason=sync screen=%s", screen_name(g_screen));
        return false;
    }
    return true;
}

void *registry_op_thread_main(void *arg)
{
    RegistryOpRequest request = *static_cast<RegistryOpRequest *>(arg);
    delete static_cast<RegistryOpRequest *>(arg);
    uint32_t start = lv_tick_get();

    std::vector<std::string> args;
    switch (request.kind) {
        case RegistryOpKind::SetRegion:
            args = {"--set-region", request.region};
            break;
        case RegistryOpKind::AddRegistry:
            args = {"--add-registry", request.url, "--registry-name", request.name};
            break;
        case RegistryOpKind::EditRegistry:
            args = {"--edit-registry", request.old_url, request.url, "--registry-name", request.name};
            break;
        case RegistryOpKind::ToggleRegistry:
            args = {request.enable ? "--enable-registry" : "--disable-registry", request.url};
            break;
        case RegistryOpKind::DeleteRegistry:
            args = {"--remove-registry", request.url};
            break;
        case RegistryOpKind::None:
        default:
            break;
    }

    app_tracef("registry_op worker begin kind=%d url=%s region=%s",
               static_cast<int>(request.kind),
               request.url.empty() ? "-" : request.url.c_str(),
               request.region.empty() ? "-" : request.region.c_str());
    int rc = -1;
    std::string out = args.empty() ? "ERROR\tunknown registry operation\n" : backend_capture(args, &rc);
    app_tracef("registry_op worker end kind=%d elapsed=%ums rc=%d bytes=%zu",
               static_cast<int>(request.kind), lv_tick_elaps(start), rc, out.size());
    pthread_mutex_lock(&g_registry_op_mutex);
    if (request.generation == g_registry_op_generation) {
        g_registry_op_result = {request, out, rc};
        g_registry_op_done = true;
    } else {
        app_tracef("registry_op worker stale kind=%d gen=%llu current=%llu",
                   static_cast<int>(request.kind),
                   static_cast<unsigned long long>(request.generation),
                   static_cast<unsigned long long>(g_registry_op_generation));
    }
    pthread_mutex_unlock(&g_registry_op_mutex);
    return nullptr;
}

void start_registry_op(const RegistryOpRequest &request, const std::string &status)
{
    app_tracef("registry_op start request kind=%d status=%s screen=%s",
               static_cast<int>(request.kind), status.c_str(), screen_name(g_screen));
    pthread_mutex_lock(&g_registry_op_mutex);
    if (g_registry_op_running) {
        pthread_mutex_unlock(&g_registry_op_mutex);
        g_status_message = "Registry operation running";
        return;
    }
    g_registry_op_running = true;
    g_registry_op_done = false;
    g_registry_op_kind = request.kind;
    uint64_t generation = ++g_registry_op_generation;
    pthread_mutex_unlock(&g_registry_op_mutex);

    g_status_message = status;
    pthread_t thread_id;
    auto *thread_arg = new RegistryOpRequest(request);
    thread_arg->generation = generation;
    if (pthread_create(&thread_id, nullptr, registry_op_thread_main, thread_arg) != 0) {
        delete thread_arg;
        pthread_mutex_lock(&g_registry_op_mutex);
        g_registry_op_running = false;
        g_registry_op_kind = RegistryOpKind::None;
        pthread_mutex_unlock(&g_registry_op_mutex);
        g_status_message = "Unable to start registry operation";
        std::fprintf(stderr, "[AppStore UI] start_registry_op failed: pthread_create\n");
        return;
    }
    pthread_detach(thread_id);
}

void apply_region_output(const std::string &out)
{
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.size() >= 4 && fields[0] == "REGION") {
            g_region_code = fields[1];
            g_region_label = fields[2];
            g_region_registry_url = fields[3];
            if (fields.size() >= 5) g_region_active = fields[4];
            return;
        }
    }
}

std::string registry_count_message(const std::string &out, bool editing)
{
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.size() >= 5 && fields[0] == "REGISTRY") {
            std::string count = fields[1] == "UPDATED" && fields.size() >= 6 ? fields[5] : fields[4];
            return (editing ? "Registry updated" : "Registry added") +
                (count.empty() ? std::string() : " (" + count + " apps)");
        }
    }
    return editing ? "Registry updated" : "Registry added";
}

bool poll_registry_op()
{
    uint32_t start = lv_tick_get();
    RegistryOpResult result;
    bool done = false;

    pthread_mutex_lock(&g_registry_op_mutex);
    if (g_registry_op_done) {
        done = true;
        result = std::move(g_registry_op_result);
        g_registry_op_done = false;
        g_registry_op_running = false;
        g_registry_op_kind = RegistryOpKind::None;
    }
    pthread_mutex_unlock(&g_registry_op_mutex);

    if (!done) return false;

    bool ok = result.rc == 0 && result.output.find("ERROR") == std::string::npos;
    std::fprintf(stderr, "[AppStore UI] poll_registry_op done kind=%d rc=%d ok=%d preview=%s\n",
                 static_cast<int>(result.request.kind), result.rc, ok ? 1 : 0,
                 one_line(result.output, 180).c_str());
    if (!ok) {
        g_status_message = one_line(backend_error_message(result.output), 54);
        app_tracef("registry_op poll fail elapsed=%ums kind=%d rc=%d message=%s",
                   lv_tick_elaps(start), static_cast<int>(result.request.kind), result.rc,
                   g_status_message.c_str());
        return true;
    }

    save_backend_registry_config_to_shared();

    switch (result.request.kind) {
        case RegistryOpKind::SetRegion:
            apply_region_output(result.output);
            g_status_message = "Region: " + g_region_label;
            request_registry_refresh();
            request_summary_refresh();
            sync_catalog(true);
            break;
        case RegistryOpKind::AddRegistry:
        case RegistryOpKind::EditRegistry:
            g_status_message = registry_count_message(
                result.output, result.request.kind == RegistryOpKind::EditRegistry);
            g_registry_edit_url.clear();
            g_screen = Screen::Registry;
            request_registry_refresh();
            sync_catalog(true);
            break;
        case RegistryOpKind::ToggleRegistry:
            g_status_message = result.request.enable ? "Registry enabled" : "Registry disabled";
            request_registry_refresh();
            request_summary_refresh();
            if (result.request.enable) sync_catalog(true);
            break;
        case RegistryOpKind::DeleteRegistry:
            g_status_message = "Registry deleted";
            g_registry_edit_url.clear();
            request_registry_refresh();
            request_summary_refresh();
            break;
        case RegistryOpKind::None:
        default:
            g_status_message = "Registry updated";
            request_registry_refresh();
            break;
    }
    app_tracef("registry_op poll ok elapsed=%ums kind=%d screen=%s status=%s",
               lv_tick_elaps(start), static_cast<int>(result.request.kind),
               screen_name(g_screen), g_status_message.c_str());
    return true;
}

void clean_root()
{
    lv_obj_clean(g_root);
    g_render_image_sources.clear();
    g_render_image_sources.reserve(12);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x080B10), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                const lv_font_t *font, uint32_t color, lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_label_set_long_mode(obj, mode);
    lv_label_set_text(obj, text.c_str());
    return obj;
}

lv_obj_t *center_label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                       const lv_font_t *font, uint32_t color,
                       lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    lv_obj_t *obj = label(parent, text, x, y, w, h, font, color, mode);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    return obj;
}

void strong_label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                  const lv_font_t *font, uint32_t color,
                  lv_label_long_mode_t mode = LV_LABEL_LONG_DOT)
{
    label(parent, text, x, y, w, h, font, color, mode);
    label(parent, text, x + 1, y, w, h, font, color, mode);
}

void center_strong_label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                         const lv_font_t *font, uint32_t color,
                         lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP)
{
    center_label(parent, text, x, y, w, h, font, color, mode);
    center_label(parent, text, x + 1, y, w, h, font, color, mode);
}

bool status_message_visible(int w)
{
    (void)w;
    if (g_status_message.empty()) {
        g_status_scroll_text.clear();
        g_status_scroll_start_tick = 0;
        g_status_scroll_hide_after_ms = 0;
        return false;
    }
    if (g_status_scroll_text != g_status_message) {
        g_status_scroll_text = g_status_message;
        g_status_scroll_start_tick = lv_tick_get();
        g_status_scroll_hide_after_ms = kStatusScrollVisibleMs;
        return true;
    }
    if (g_status_scroll_start_tick != 0 &&
        lv_tick_elaps(g_status_scroll_start_tick) >= g_status_scroll_hide_after_ms) {
        g_status_message.clear();
        g_status_scroll_text.clear();
        g_status_scroll_start_tick = 0;
        g_status_scroll_hide_after_ms = 0;
        return false;
    }
    return true;
}

void scrolling_status_label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int h,
                            uint32_t color = 0xCCCC33)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_obj_set_style_anim_duration(obj, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(obj, text.c_str());
}

bool draw_packaged_image(const std::string &name, int x, int y);

void box(int x, int y, int w, int h, uint32_t color, uint32_t border = 0x2A3A46,
         int border_width = 1, int radius = 0, lv_opa_t bg_opa = LV_OPA_COVER)
{
    lv_obj_t *obj = lv_obj_create(g_root);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, bg_opa, 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
}

void draw_system_bar()
{
    uint32_t start = lv_tick_get();
    update_top_status_cache();
    uint32_t status_ms = lv_tick_elaps(start);
    g_top_status_last_render_tick = lv_tick_get();

    auto clear_status_panel_style = [](lv_obj_t *obj) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    };

    lv_obj_t *bar = lv_obj_create(g_root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 320, 20);
    lv_obj_clear_flag(bar, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, "AppStore");
    lv_obj_set_size(title, 150, 20);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xCCAA00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 0, 20);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    bool sync_active = false;
    pthread_mutex_lock(&g_sync_mutex);
    sync_active = g_sync_running;
    pthread_mutex_unlock(&g_sync_mutex);
    if (!sync_active && g_sync_visible_until_tick != 0 &&
        lv_tick_elaps(g_sync_visible_until_tick) < 1000) {
        sync_active = true;
    }
    const cp0_wifi_status_t &wifi_status = g_top_wifi_status;
    if (sync_active) {
        int phase = static_cast<int>((lv_tick_get() / kSyncAnimRefreshMs) % 4);
        lv_obj_t *sync_label = lv_label_create(bar);
        lv_label_set_text(sync_label, "SYNC");
        lv_obj_set_size(sync_label, 24, 12);
        lv_obj_set_style_text_font(sync_label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(sync_label, lv_color_hex(0x33CC33), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(sync_label, phase < 2 ? 255 : 90, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(sync_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(sync_label, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    }

    lv_obj_t *eth_icon = lv_img_create(bar);
    lv_img_set_src(eth_icon, cp0_file_path_c("status_ethernet.png"));
    lv_obj_clear_flag(eth_icon, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    if (!wifi_status.ethernet) {
        lv_obj_add_flag(eth_icon, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *wifi_panel = lv_obj_create(bar);
    lv_obj_set_size(wifi_panel, 22, 15);
    clear_status_panel_style(wifi_panel);
    lv_obj_set_flex_flow(wifi_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(wifi_panel, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (!wifi_status.connected) {
        lv_obj_add_flag(wifi_panel, LV_OBJ_FLAG_HIDDEN);
    }
    static const int bar_heights[4] = {6, 9, 12, 15};
    static const int thresholds[4] = {1, 30, 60, 80};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *signal_bar = lv_obj_create(wifi_panel);
        lv_obj_remove_style_all(signal_bar);
        lv_obj_set_size(signal_bar, 4, bar_heights[i]);
        lv_obj_clear_flag(signal_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(signal_bar, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        uint32_t color = wifi_status.connected && wifi_status.signal >= thresholds[i] ? 0x33CC33 : 0x4D4D4D;
        lv_obj_set_style_bg_color(signal_bar, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(signal_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(signal_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_obj_t *time_panel = lv_obj_create(bar);
    lv_obj_set_size(time_panel, 40, 16);
    clear_status_panel_style(time_panel);
    lv_obj_set_style_bg_img_src(time_panel, cp0_file_path_c("status_time_background.png"),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *time_label = lv_label_create(time_panel);
    lv_obj_set_align(time_label, LV_ALIGN_CENTER);
    char time_buf[16] = "--:--";
    cp0_time_str(time_buf, sizeof(time_buf));
    lv_label_set_text(time_label, time_buf);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(time_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *battery_panel = lv_obj_create(bar);
    lv_obj_set_size(battery_panel, 36, 16);
    clear_status_panel_style(battery_panel);
    lv_obj_set_style_bg_img_src(battery_panel, cp0_file_path_c("status_battery_background.png"),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *battery_bar = lv_bar_create(battery_panel);
    const cp0_battery_info_t &bat = g_top_battery_status;
    const bool charging = (bat.flags & 1) != 0;
    int soc = bat.valid ? bat.soc : 100;
    soc = std::max(0, std::min(100, soc));
    lv_bar_set_value(battery_bar, soc, LV_ANIM_OFF);
    lv_bar_set_start_value(battery_bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(battery_bar, 33, 14);
    lv_obj_set_align(battery_bar, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(battery_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x484847),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(battery_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x66CC33),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (charging) {
        lv_obj_t *charge_wave = lv_obj_create(battery_panel);
        int wave_x = -8 + static_cast<int>((lv_tick_get() % 850) * 44 / 850);
        lv_obj_set_size(charge_wave, 8, 14);
        lv_obj_set_pos(charge_wave, wave_x, 1);
        lv_obj_clear_flag(charge_wave, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_style_radius(charge_wave, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(charge_wave, lv_color_hex(0xFFF176), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(charge_wave, 190, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(charge_wave, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_t *power_label = lv_label_create(battery_panel);
    lv_obj_set_align(power_label, LV_ALIGN_CENTER);
    char pwr_buf[16];
    std::snprintf(pwr_buf, sizeof(pwr_buf), "%d%%", soc);
    lv_label_set_text(power_label, pwr_buf);
    lv_obj_set_style_text_color(power_label, lv_color_hex(charging ? 0xFFF2A8 : 0xFFFFFF),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(power_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_move_foreground(power_label);
    uint32_t elapsed = lv_tick_elaps(start);
    if (elapsed >= 15 || g_screen == Screen::Registry || g_screen == Screen::RegistryEdit) {
        app_tracef("draw_system_bar elapsed=%ums status=%ums screen=%s",
                   elapsed, status_ms, screen_name(g_screen));
    }
}

std::string app_initial(const StoreApp &app)
{
    for (char ch : app.name) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            return std::string(1, static_cast<char>(std::toupper(uch)));
        }
    }
    return "A";
}

bool draw_app_icon_image(const StoreApp &app)
{
#if LV_USE_LODEPNG && LV_USE_FS_POSIX
    std::string path = icon_file_path(g_app_dir, app);
    if (path.empty()) {
        app_tracef("image icon missing app=%s images=%s", app.id.c_str(),
                   app.images.empty() ? "-" : one_line(app.images, 120).c_str());
        return false;
    }
    std::string src = lvgl_posix_src(path);
    g_render_image_sources.push_back(src);
    app_tracef("image icon app=%s path=%s probe=%s src=%s",
               app.id.c_str(), path.c_str(), probe_file_for_log(path).c_str(), src.c_str());

    constexpr int clip_x = kHomeIconX;
    constexpr int clip_y = kHomeIconY;
    constexpr int clip_w = kHomeIconSize;
    constexpr int clip_h = kHomeIconSize;
    lv_obj_t *clip = lv_obj_create(g_root);
    lv_obj_remove_style_all(clip);
    lv_obj_set_pos(clip, clip_x, clip_y);
    lv_obj_set_size(clip, clip_w, clip_h);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clip, 0, 0);
    lv_obj_set_style_radius(clip, 10, 0);
    lv_obj_set_style_clip_corner(clip, true, 0);
    lv_obj_set_style_pad_all(clip, 0, 0);

    lv_obj_t *icon = lv_image_create(clip);
    lv_image_set_scale(icon, 174);
    lv_image_set_src(icon, g_render_image_sources.back().c_str());
    lv_obj_update_layout(icon);

    int icon_w = lv_obj_get_width(icon);
    int icon_h = lv_obj_get_height(icon);
    if (icon_w <= 0) icon_w = 100;
    if (icon_h <= 0) icon_h = 100;

    lv_obj_set_pos(icon, (clip_w - icon_w) / 2, (clip_h - icon_h) / 2);
    return true;
#else
    (void)app;
    return false;
#endif
}

bool draw_packaged_image(const std::string &name, int x, int y)
{
#if LV_USE_LODEPNG && LV_USE_FS_POSIX
    const char *path = cp0_file_path_c(name.c_str());
    if (!path || !path[0]) return false;
    lv_obj_t *image = lv_image_create(g_root);
    lv_image_set_src(image, path);
    lv_obj_set_pos(image, x, y);
    return true;
#else
    (void)name;
    (void)x;
    (void)y;
    return false;
#endif
}

void normalize_detail_image_state(const StoreApp &app, const std::vector<std::string> &screenshots)
{
    if (g_detail_media_app_id != app.id) {
        g_detail_media_app_id = app.id;
        g_detail_image_index = 0;
    }
    if (screenshots.empty()) {
        g_detail_image_index = 0;
    } else {
        int count = static_cast<int>(screenshots.size());
        if (g_detail_image_index < 0) g_detail_image_index = count - 1;
        if (g_detail_image_index >= count) g_detail_image_index = 0;
    }
}

void normalize_detail_description_state(const StoreApp &app, const std::vector<std::string> &lines)
{
    if (g_detail_description_app_id != app.id) {
        g_detail_description_app_id = app.id;
        g_detail_description_scroll = 0;
    }
    int max_scroll = std::max(0, static_cast<int>(lines.size()) - 2);
    if (g_detail_description_scroll < 0) g_detail_description_scroll = 0;
    if (g_detail_description_scroll > max_scroll) g_detail_description_scroll = max_scroll;
}

void show_screenshots_overlay()
{
    g_screenshots_overlay_visible = true;
    g_screenshots_activity_tick = lv_tick_get();
}

bool draw_detail_background(const StoreApp &app)
{
#if LV_USE_LODEPNG && LV_USE_FS_POSIX
    std::vector<std::string> screenshots = detail_screenshot_paths(g_app_dir, app);
    normalize_detail_image_state(app, screenshots);
    if (screenshots.empty()) {
        app_tracef("image screenshot missing app=%s images=%s", app.id.c_str(),
                   app.images.empty() ? "-" : one_line(app.images, 120).c_str());
        return false;
    }

    std::string src = lvgl_posix_src(screenshots[g_detail_image_index]);
    g_render_image_sources.push_back(src);
    app_tracef("image screenshot app=%s index=%d path=%s probe=%s src=%s",
               app.id.c_str(), g_detail_image_index,
               screenshots[g_detail_image_index].c_str(),
               probe_file_for_log(screenshots[g_detail_image_index]).c_str(), src.c_str());
    lv_obj_t *clip = lv_obj_create(g_root);
    lv_obj_remove_style_all(clip);
    lv_obj_set_pos(clip, 0, 0);
    lv_obj_set_size(clip, kScreenWidth, kScreenHeight);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clip, 0, 0);
    lv_obj_set_style_pad_all(clip, 0, 0);

    lv_obj_t *image = lv_image_create(clip);
    lv_image_set_src(image, g_render_image_sources.back().c_str());
    lv_obj_update_layout(image);
    int image_w = lv_obj_get_width(image);
    int image_h = lv_obj_get_height(image);
    if (image_w <= 0) image_w = kScreenWidth;
    if (image_h <= 0) image_h = kScreenHeight;
    int scale_x = kScreenWidth * 256 / image_w;
    int scale_y = kScreenHeight * 256 / image_h;
    int scale = std::max(scale_x, scale_y);
    if (scale <= 0) scale = 256;
    lv_image_set_scale(image, static_cast<uint16_t>(scale));
    lv_obj_update_layout(image);
    int scaled_w = image_w * scale / 256;
    int scaled_h = image_h * scale / 256;
    lv_obj_set_pos(image, (kScreenWidth - scaled_w) / 2, (kScreenHeight - scaled_h) / 2);
    return true;
#else
    (void)app;
    return false;
#endif
}

void draw_home_icon_panel(const StoreApp *app)
{
    const int arrow_x = kHomeIconX + (kHomeIconSize - 14) / 2;
    if (!draw_packaged_image("store_arrow_up.png", arrow_x, 32)) {
        center_strong_label(g_root, "^", kHomeIconX + 20, 31, 28, 18, &lv_font_montserrat_20, 0xFF6A3D);
    }
    box(kHomeIconX, kHomeIconY, kHomeIconSize, kHomeIconSize, 0x202020, 0x4D4D4D, 2, 10, LV_OPA_COVER);
    if (!app) {
        center_strong_label(g_root, "-", kHomeIconX + 22, kHomeIconY + 24, 24, 22,
                            &lv_font_montserrat_20, 0x9A9A9A);
        if (!draw_packaged_image("store_arrow_down.png", arrow_x, 119)) {
            center_strong_label(g_root, "v", kHomeIconX + 20, 119, 28, 18, &lv_font_montserrat_20, 0xFF6A3D);
        }
        return;
    }

    if (!draw_app_icon_image(*app)) {
        center_strong_label(g_root, app_initial(*app), kHomeIconX, kHomeIconY + 21, kHomeIconSize, 28,
                            &lv_font_montserrat_20, app->installed ? 0x52D05D : 0xFFFFFF);
    }
    if (!draw_packaged_image("store_arrow_down.png", arrow_x, 119)) {
        center_strong_label(g_root, "v", kHomeIconX + 20, 119, 28, 18, &lv_font_montserrat_20, 0xFF6A3D);
    }
}

void *summary_thread_main(void *arg)
{
    SummaryRequest request = *static_cast<SummaryRequest *>(arg);
    delete static_cast<SummaryRequest *>(arg);
    SummaryData summary = load_summary(request.rule);
    pthread_mutex_lock(&g_summary_mutex);
    if (request.generation == g_summary_generation) {
        g_summary_result = std::move(summary);
        g_summary_done = true;
        g_summary_running = false;
    } else {
        app_tracef("summary worker stale gen=%llu current=%llu",
                   static_cast<unsigned long long>(request.generation),
                   static_cast<unsigned long long>(g_summary_generation));
    }
    pthread_mutex_unlock(&g_summary_mutex);
    return nullptr;
}

void request_summary_refresh()
{
    std::fprintf(stderr, "[AppStore UI] request_summary_refresh\n");
    pthread_mutex_lock(&g_summary_mutex);
    if (g_summary_running) {
        g_summary_pending = true;
        pthread_mutex_unlock(&g_summary_mutex);
        std::fprintf(stderr, "[AppStore UI] request_summary_refresh pending: already running\n");
        return;
    }
    g_summary_running = true;
    g_summary_done = false;
    SortRule rule_value = g_sort_rule;
    g_summary_rule = rule_value;
    uint64_t generation = ++g_summary_generation;
    pthread_mutex_unlock(&g_summary_mutex);

    pthread_t thread_id;
    auto *thread_arg = new SummaryRequest{rule_value, generation};
    if (pthread_create(&thread_id, nullptr, summary_thread_main, thread_arg) != 0) {
        delete thread_arg;
        pthread_mutex_lock(&g_summary_mutex);
        g_summary_running = false;
        pthread_mutex_unlock(&g_summary_mutex);
        g_status_message = "Unable to refresh app list";
        std::fprintf(stderr, "[AppStore UI] request_summary_refresh failed: pthread_create\n");
        return;
    }
    pthread_detach(thread_id);
    std::fprintf(stderr, "[AppStore UI] request_summary_refresh thread started rule=%d\n",
                 static_cast<int>(rule_value));
}

bool poll_summary_refresh()
{
    SummaryData summary;
    bool done = false;
    bool start_next = false;

    pthread_mutex_lock(&g_summary_mutex);
    if (g_summary_done) {
        done = true;
        summary = std::move(g_summary_result);
        g_summary_done = false;
        start_next = g_summary_pending;
        g_summary_pending = false;
    }
    pthread_mutex_unlock(&g_summary_mutex);

    if (!done) return false;

    std::fprintf(stderr, "[AppStore UI] poll_summary_refresh done start_next=%d apps=%zu cats=%zu warning=%s\n",
                 start_next ? 1 : 0, summary.apps.size(), summary.categories.size(),
                 summary.warning.empty() ? "-" : summary.warning.c_str());
    apply_summary(summary);
    if (start_next) request_summary_refresh();
    return true;
}

void draw_category_selector()
{
    if (!draw_packaged_image("store_arrow_left.png", 205, 34)) {
        strong_label(g_root, "<", 206, 32, 14, 18, &lv_font_montserrat_20, 0xFF6A3D);
    }
    std::string cat = current_category_name();
    std::string cat_label = upper_ascii(cat);
    const lv_font_t *cat_font = cat_label.size() > 8 ? &lv_font_montserrat_10 : &lv_font_montserrat_14;
    center_strong_label(g_root, one_line(cat_label, 13), 220, 35, 72, 15,
                        cat_font, 0xFFFFFF, LV_LABEL_LONG_DOT);
    if (!draw_packaged_image("store_arrow_right.png", 298, 34)) {
        strong_label(g_root, ">", 300, 32, 14, 18, &lv_font_montserrat_20, 0xFF6A3D);
    }
}

int shortcut_screen_x(int spec_center_x)
{
    return spec_center_x - kShortcutSpecScreenLeft;
}

void draw_shortcut_button(int spec_center_x, const std::string &icon_name,
                          const std::string &text, uint32_t text_color)
{
    const int cx = shortcut_screen_x(spec_center_x);
    const int icon_x = cx - kShortcutIconSize / 2;
    const int icon_y = kShortcutCenterY - 13;
    if (!draw_packaged_image(icon_name, icon_x, icon_y)) {
        center_strong_label(g_root, "*", cx - 8, icon_y + 1, 16, 10,
                            &lv_font_montserrat_10, text_color);
    }
    center_strong_label(g_root, text, cx - kShortcutTextWidth / 2, kShortcutCenterY + 5,
                        kShortcutTextWidth, 10,
                        &lv_font_montserrat_10, text_color, LV_LABEL_LONG_DOT);
}

void draw_shortcut_button_if(bool visible, int spec_center_x, const std::string &icon_name,
                             const std::string &text, uint32_t text_color)
{
    if (visible) draw_shortcut_button(spec_center_x, icon_name, text, text_color);
}

void draw_shortcuts_bar()
{
    draw_shortcut_button(180, "appstore-shortcut-settings.png", "settings", 0xF0B429);
    draw_shortcut_button(236, "appstore-shortcut-sharecode.png", "share", 0x168CE5);
    draw_shortcut_button(292, "appstore-shortcut-search.png", "search", 0x7ED957);
    draw_shortcut_button(348, "appstore-shortcut-install.png", "install", 0xB069FF);
    draw_shortcut_button(404, "appstore-shortcut-detail.png", "detail", 0xFF8A2A);
}

void draw_detail_shortcuts(const StoreApp &app)
{
    draw_shortcut_button(180, "appstore-shortcut-back.png", "back", 0xF0B429);
    draw_shortcut_button_if(!detail_screenshot_paths(g_app_dir, app).empty(), 236,
                            "appstore-shortcut-screenshots.png", "shots", 0x58A6FF);
    draw_shortcut_button_if(!app.installed && can_install_app(app), 292,
                            "appstore-shortcut-install.png", "install", 0xB069FF);
    draw_shortcut_button_if(can_reinstall_app(app), 292,
                            "appstore-shortcut-install.png", "reinstall", 0xB069FF);
    draw_shortcut_button_if(can_upgrade_app(app), 348,
                            "appstore-shortcut-upgrade.png", "upgrade", 0xB069FF);
    draw_shortcut_button_if(app.installed, 404,
                            "appstore-shortcut-delete.png", "remove", 0xFF5B4A);
}

void header(const std::string &title, const std::string &right)
{
    draw_system_bar();
    label(g_root, title, 8, 22, 160, 15, &lv_font_montserrat_14, 0xF4F8FB);
    label(g_root, right, 170, 23, 142, 13, &lv_font_montserrat_10, 0x8BE28B, LV_LABEL_LONG_DOT);
}

void render_home()
{
    clean_root();
    draw_system_bar();
    draw_category_selector();
    draw_home_icon_panel(selected_app());

    static size_t last_apps = static_cast<size_t>(-1);
    static size_t last_visible = static_cast<size_t>(-1);
    static int last_category = -9999;
    static int last_selected = -9999;
    static std::string last_status;
    if (last_apps != g_apps.size() || last_visible != g_visible.size() ||
        last_category != g_category || last_selected != g_selected ||
        last_status != g_status_message) {
        std::fprintf(stderr,
                     "[AppStore UI] render_home apps=%zu visible=%zu category=%s index=%d selected=%d status=%s\n",
                     g_apps.size(), g_visible.size(), current_category_name().c_str(), g_category,
                     g_selected, g_status_message.empty() ? "-" : g_status_message.c_str());
        last_apps = g_apps.size();
        last_visible = g_visible.size();
        last_category = g_category;
        last_selected = g_selected;
        last_status = g_status_message;
    }

    if (g_visible.empty()) {
        strong_label(g_root, "NO APPS", 106, 72, 130, 24, &lv_font_montserrat_20, 0xFFFFFF);
    } else {
        const int list_x = 106;
        const int y_pos[] = {39, 55, 70, 104, 119};
        const lv_font_t *fonts[] = {
            &lv_font_montserrat_10, &lv_font_montserrat_12, &lv_font_montserrat_20,
            &lv_font_montserrat_12, &lv_font_montserrat_10
        };
        const uint32_t colors[] = {0x3D3D3D, 0x575757, 0xFFFFFF, 0x575757, 0x3D3D3D};
        struct HomeRow {
            int row;
            int visible_index;
        };
        std::vector<HomeRow> rows;
        int visible_count = static_cast<int>(g_visible.size());
        if (visible_count >= 5) {
            for (int row = 0; row < 5; ++row) {
                HomeRow item = {row, (g_selected + row - 2 + visible_count) % visible_count};
                rows.push_back(item);
            }
        } else {
            int first = std::max(0, g_selected - 2);
            int last = std::min(visible_count - 1, first + 4);
            first = std::max(0, last - 4);
            int start_row = 2 - (g_selected - first);
            for (int visible_index = first; visible_index <= last; ++visible_index) {
                int row = start_row + (visible_index - first);
                if (row >= 0 && row < 5) {
                    HomeRow item = {row, visible_index};
                    rows.push_back(item);
                }
            }
        }
        for (const auto &entry : rows) {
            int row = entry.row;
            int visible_index = entry.visible_index;
            const StoreApp &app = g_apps[g_visible[visible_index]];
            bool selected = visible_index == g_selected;
            std::string text = selected ? upper_ascii(app.name) : one_line(upper_ascii(app.name), 16);
            strong_label(g_root, text, list_x, y_pos[row], selected ? 144 : 130,
                         selected ? 24 : 16, font_for_text(text, fonts[row]), colors[row],
                         selected ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
            if (selected) {
                strong_label(g_root, "V" + one_line(app.version.empty() ? "0" : app.version, 6),
                             264, y_pos[row] + 3, 52, 16, &lv_font_montserrat_14, 0x8B8B8B,
                             LV_LABEL_LONG_DOT);
                std::string author = app.author.empty() ? "unknown" : app.author;
                strong_label(g_root, author + ".",
                             list_x + 3, y_pos[row] + 22, 190, 12,
                             &lv_font_montserrat_10, 0xBDBDBD);
            }
        }
        std::string count = std::to_string(g_selected + 1) + "/" + std::to_string(g_visible.size());
        strong_label(g_root, count, 284, 119, 34, 15, &lv_font_montserrat_14, 0xFFFFFF,
                     LV_LABEL_LONG_DOT);
    }

    if (status_message_visible(172)) {
        scrolling_status_label(g_root, g_status_message, 106, 128, 172, 10);
    }
    draw_shortcuts_bar();
}

void render_detail()
{
    clean_root();
    StoreApp *app = ensure_selected_app();
    if (!app) {
        draw_system_bar();
        box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
        label(g_root, "No selected app", 10, 50, 304, 14, &lv_font_montserrat_12, 0xE6EDF3);
        return;
    }
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    std::string title = one_line(app->name + "  " + app->version, 30);
    label(g_root, title, 8, 24, 220, 15,
          font_for_text(title, &lv_font_montserrat_12), 0xFFFFFF, LV_LABEL_LONG_DOT);
    label(g_root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);

    std::string state_text = app->installed ? "Installed" : "Not installed";
    if (app->installed && !app->installed_version.empty()) {
        state_text += " " + one_line(app->installed_version, 10);
    }
    label(g_root, "State :", 10, 47, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, state_text, 62, 47, 105, 12,
          &lv_font_montserrat_10, app->installed ? 0xCCCC33 : 0xE6EDF3);
    label(g_root, "Size :", 176, 47, 42, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, app->size, 220, 47, 84, 12, &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);

    label(g_root, "Review:", 10, 62, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(review_label(*app), 36), 62, 62, 246, 12,
          &lv_font_montserrat_10, can_install_app(*app) ? 0x52D05D : 0xF0C45C, LV_LABEL_LONG_DOT);
    label(g_root, "Author:", 10, 77, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(app->author.empty() ? "-" : app->author, 36), 62, 77, 246, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(g_root, "Git   :", 10, 92, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(app->git_url.empty() ? "-" : app->git_url, 42), 62, 92, 246, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(g_root, "Deps  :", 10, 107, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(app->dependencies.empty() ? "-" : app->dependencies, 42), 62, 107, 246, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    if (!g_job_running && status_message_visible(300)) {
        box(8, 119, 304, 13, 0x0D1117, 0x0D1117, 0, 0, LV_OPA_COVER);
        scrolling_status_label(g_root, g_status_message, 10, 121, 300, 10);
    } else {
        std::vector<std::string> lines = detail_description_lines(*app);
        if (lines.empty()) lines.push_back("-");
        normalize_detail_description_state(*app, lines);
        const int total = static_cast<int>(lines.size());
        const int first = std::min(g_detail_description_scroll, std::max(0, total - 1));
        const int visible_count = std::min(2, total - first);
        const int text_width = total > 2 ? 268 : 300;
        for (int row = 0; row < visible_count; ++row) {
            const std::string &line_text = lines[first + row];
            label(g_root, line_text, 10, 120 + row * 10, text_width, 10,
                  font_for_text(line_text, &lv_font_montserrat_10), 0xB8B8B8, LV_LABEL_LONG_DOT);
        }
        if (total > 2) {
            int max_scroll = std::max(0, total - 2);
            std::string marker = std::to_string(g_detail_description_scroll + 1) + "/" +
                                 std::to_string(max_scroll + 1);
            center_label(g_root, marker, 280, 130, 34, 10,
                         &lv_font_montserrat_10, 0x6E7681, LV_LABEL_LONG_DOT);
        }
    }
    if (g_job_running) {
        box(10, 135, 300, 5, 0x30363D, 0x30363D, 0);
        if (g_job_progress >= 0) {
            int fill = std::max(2, std::min(300, g_job_progress * 300 / 100));
            box(10, 135, fill, 5, 0xCCCC33, 0xCCCC33, 0);
        } else {
            int offset = static_cast<int>((lv_tick_get() / 120) % 260);
            box(10 + offset, 135, 40, 5, 0xCCCC33, 0xCCCC33, 0);
        }
        label(g_root, one_line(g_status_message.empty() ? "Working..." : g_status_message, 54),
              10, 143, 300, 12, &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
        return;
    }
    draw_detail_shortcuts(*app);
}

void render_confirm()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);

    box(22, 37, 276, 105, 0x111923, 0x58A6FF, 2, 3, LV_OPA_COVER);
    box(22, 37, 276, 23, 0x1F6FEB, 0x1F6FEB, 0);
    center_strong_label(g_root, "CONFIRM ACTION", 42, 42, 236, 15, &lv_font_montserrat_12, 0xFFFFFF);

    int y = 68;
    for (const auto &line : g_confirm_lines) {
        label(g_root, one_line(line, 42), 35, y, 250, 12, &lv_font_montserrat_10, 0xE6EDF3,
              LV_LABEL_LONG_DOT);
        y += 14;
        if (y > 109) break;
    }

    const bool yes_focused = g_confirm_focus == 0;
    const bool no_focused = g_confirm_focus == 1;
    box(54, 119, 84, 17, yes_focused ? 0x2EA043 : 0x1A6A2A,
        yes_focused ? 0xCCCC33 : 0x2EA043, yes_focused ? 2 : 1);
    center_strong_label(g_root, "Y YES", 58, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    box(182, 119, 84, 17, no_focused ? 0x8B2F34 : 0x4B1F24,
        no_focused ? 0xCCCC33 : 0xF85149, no_focused ? 2 : 1);
    center_strong_label(g_root, "N NO", 186, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    center_strong_label(g_root, "Left/Right or Tab select   Enter confirm", 28, 149, 264, 12,
                        &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_sudo_password()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(22, 37, 276, 105, 0x111923, 0xCCCC33, 2, 3, LV_OPA_COVER);
    box(22, 37, 276, 23, 0x7A5C00, 0x7A5C00, 0);
    center_strong_label(g_root, "SUDO PASSWORD", 42, 42, 236, 15,
                        &lv_font_montserrat_12, 0xFFFFFF);

    std::string verb = upper_ascii(job_action_label(g_sudo_action.empty() ? g_confirm_action : g_sudo_action));
    center_label(g_root, one_line(verb + " " + g_sudo_app_title, 36), 34, 66, 252, 14,
                 &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);

    box(50, 86, 220, 30, 0x0D1117, 0xCCCC33, 1, 2);
    std::string masked(g_sudo_password_input.size(), '*');
    if (masked.empty()) masked = "password";
    center_strong_label(g_root, one_line(masked, 24), 58, 94, 204, 15,
                        &lv_font_montserrat_12,
                        g_sudo_password_input.empty() ? 0x6E7681 : 0xFFFFFF,
                        LV_LABEL_LONG_DOT);

    center_label(g_root, one_line(g_sudo_message, 44), 34, 122, 252, 12,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    label(g_root, "Enter Run   Backspace Delete   Esc Back", 10, 153, 300, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_progress()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Package Operation", 8, 24, 190, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "Esc Exit", 262, 24, 52, 14, &lv_font_montserrat_10, 0xAECBFA);

    std::string title = g_job_title.empty() ? "Selected app" : g_job_title;
    center_strong_label(g_root, upper_ascii(job_action_label(g_job_action)), 24, 50, 272, 18,
                        &lv_font_montserrat_14, 0xCCCC33, LV_LABEL_LONG_DOT);
    std::string job_title = one_line(title, 26);
    center_strong_label(g_root, job_title, 24, 70, 272, 16,
                        font_for_text(job_title, &lv_font_montserrat_12), 0xE6EDF3, LV_LABEL_LONG_DOT);

    box(28, 92, 264, 12, 0x30363D, 0x30363D, 0, 2);
    if (g_job_progress >= 0) {
        int fill = std::max(4, std::min(264, g_job_progress * 264 / 100));
        box(28, 92, fill, 12, 0xCCCC33, 0xCCCC33, 0, 2);
    } else {
        int offset = static_cast<int>((lv_tick_get() / 90) % 216);
        box(28 + offset, 92, 48, 12, 0xCCCC33, 0xCCCC33, 0, 2);
    }

    std::string detail = g_job_pending_start ? "Preparing package worker" :
                         (g_job_detail.empty() ? "Waiting for package output" : g_job_detail);
    if (g_job_progress >= 0) detail += " " + std::to_string(g_job_progress) + "%";
    center_label(g_root, one_line(detail, 48), 18, 113, 284, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);

    uint32_t elapsed = g_job_start_tick ? lv_tick_elaps(g_job_start_tick) / 1000 : 0;
    center_strong_label(g_root, "Elapsed " + std::to_string(elapsed) + "s", 88, 132, 144, 13,
                        &lv_font_montserrat_10, 0x58A6FF, LV_LABEL_LONG_DOT);
    center_label(g_root, "Keep AppStore open until this finishes.", 18, 153, 284, 12,
                 &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_error_dialog()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);

    box(18, 34, 284, 116, 0x111923, 0xF85149, 2, 4, LV_OPA_COVER);
    box(18, 34, 284, 23, 0x8B2F34, 0x8B2F34, 0);
    center_strong_label(g_root, g_error_title.empty() ? "OPERATION FAILED" : g_error_title,
                        32, 39, 256, 15, &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);

    std::string message = g_error_message.empty() ? "Package operation failed." : g_error_message;
    center_strong_label(g_root, one_line(message, 38), 30, 68, 260, 15,
                        &lv_font_montserrat_12, 0xFFD2D2, LV_LABEL_LONG_DOT);

    std::string detail = g_error_detail.empty() ? "Please try again after checking the package state." :
                         g_error_detail;
    std::vector<std::string> lines = wrap_display_text(detail, 44);
    int y = 89;
    for (size_t i = 0; i < lines.size() && i < 3; ++i) {
        const auto &line = lines[i];
        center_label(g_root, one_line(line, 44), 30, y, 260, 12,
                     &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
        y += 13;
    }

    box(118, 128, 84, 17, 0x2EA043, 0xCCCC33, 2, 2);
    center_strong_label(g_root, "OK", 122, 131, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    center_label(g_root, "Enter OK", 102, 154, 116, 12, &lv_font_montserrat_10, 0xCCCC33);
}

void render_startup_sync()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x080B10, 0x080B10, 0);
    box(0, 20, 320, 22, 0x101B2D, 0x101B2D, 0);
    label(g_root, "Syncing Catalog", 8, 24, 190, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "Esc Exit", 262, 24, 52, 14, &lv_font_montserrat_10, 0xAECBFA);

    std::string phase = g_sync_status.phase.empty() ? "registry" : g_sync_status.phase;
    center_strong_label(g_root, upper_ascii(phase), 28, 47, 264, 16,
                        &lv_font_montserrat_12, 0x58A6FF, LV_LABEL_LONG_DOT);

    std::string url = g_sync_status.url.empty() ? g_region_registry_url : g_sync_status.url;
    label(g_root, one_line(url, 62), 18, 68, 284, 20,
          &lv_font_montserrat_10, 0xC9D1D9, LV_LABEL_LONG_DOT);

    box(20, 94, 280, 15, 0x111923, 0x30363D, 1, 3);
    for (int i = 0; i < 7; ++i) {
        int x = 24 + i * 39;
        box(x, 98, 28, 7, 0x162235, 0x162235, 0, 2);
    }
    if (g_sync_status.percent >= 0) {
        int fill = std::max(5, std::min(272, g_sync_status.percent * 272 / 100));
        box(24, 98, fill, 7, 0x2FEC8D, 0x2FEC8D, 0, 2);
        int spark = std::max(24, std::min(286, 24 + fill - 4));
        box(spark, 96, 8, 11, 0xD8FF6A, 0xD8FF6A, 0, 3);
    } else {
        int scan = static_cast<int>((lv_tick_get() / 85) % 224);
        box(24 + scan, 97, 52, 9, 0x2FEC8D, 0xD8FF6A, 1, 3);
        int tail = std::max(24, 24 + scan - 18);
        box(tail, 99, 18, 5, 0x1F6FEB, 0x1F6FEB, 0, 2);
    }

    std::string detail = g_sync_status.detail.empty() ? "Connecting to registry..." : g_sync_status.detail;
    if (g_sync_status.percent >= 0) detail += " " + std::to_string(g_sync_status.percent) + "%";
    center_label(g_root, one_line(detail, 52), 18, 119, 284, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);

    std::string cancel = g_sync_status.cancel_requested ? "Cancelling..." : "S Settings   4 Settings   Esc Exit";
    center_strong_label(g_root, cancel, 18, 151, 284, 12,
                        &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);

    if (!g_startup_sync_failed) return;

    box(18, 39, 284, 108, 0x111923, 0xF85149, 2, 4, LV_OPA_COVER);
    box(18, 39, 284, 23, 0x8B2F34, 0x8B2F34, 0);
    center_strong_label(g_root, g_error_title.empty() ? "NETWORK FAILED" : g_error_title,
                        32, 44, 256, 15, &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);

    std::string message = g_error_message.empty() ? "Unable to sync catalog." : g_error_message;
    center_strong_label(g_root, one_line(message, 38), 30, 73, 260, 15,
                        &lv_font_montserrat_12, 0xFFD2D2, LV_LABEL_LONG_DOT);

    std::string alert_detail = g_error_detail.empty() ? "Check Wi-Fi, then open AppStore again." : g_error_detail;
    std::vector<std::string> lines = wrap_display_text(alert_detail, 44);
    int y = 94;
    for (size_t i = 0; i < lines.size() && i < 2; ++i) {
        center_label(g_root, one_line(lines[i], 44), 30, y, 260, 12,
                     &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
        y += 13;
    }

    box(118, 124, 84, 17, 0x2EA043, 0xCCCC33, 2, 2);
    center_strong_label(g_root, "OK", 122, 127, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    center_label(g_root, "Enter OK", 102, 153, 116, 12, &lv_font_montserrat_10, 0xCCCC33);
}

void draw_radio_option(int x, int y, const std::string &text, bool selected, bool focused)
{
    uint32_t border = focused ? 0xCCCC33 : (selected ? 0x58A6FF : 0x6E7681);
    uint32_t text_color = selected ? 0xFFFFFF : 0x8B949E;
    box(x, y + 2, 10, 10, 0x0D1117, border, 1, 5);
    if (selected) {
        box(x + 3, y + 5, 4, 4, focused ? 0xCCCC33 : 0x58A6FF,
            focused ? 0xCCCC33 : 0x58A6FF, 0, 2);
    }
    label(g_root, text, x + 15, y, 70, 12, &lv_font_montserrat_10,
          text_color, LV_LABEL_LONG_DOT);
}

void render_registry()
{
    TraceScope trace("render_registry", true);
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Registry Settings", 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "B Back", 270, 24, 44, 14, &lv_font_montserrat_10, 0xAECBFA);

    bool region_focused = g_registry_page_focus == 0;
    label(g_root, "REGION", 24, 45, 45, 12, &lv_font_montserrat_10,
          region_focused ? 0xCCCC33 : 0x58A6FF);
    draw_radio_option(76, 45, "Auto", g_region_code == "auto", region_focused);
    draw_radio_option(142, 45, "Default", g_region_code == "default", region_focused);
    draw_radio_option(234, 45, "CN", g_region_code == "CN", region_focused);
    if (g_region_code == "auto") {
        label(g_root, "using " + g_region_active, 248, 58, 60, 12, &lv_font_montserrat_10,
              0x8B949E, LV_LABEL_LONG_DOT);
    }

    if (g_registry_entries.empty()) {
        label(g_root, g_registry_loading ? "Loading registries..." : "No registries configured.", 10, 82, 300, 14,
              &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    } else {
        const RegistryEntry &entry = g_registry_entries[g_registry_selected];
        center_label(g_root, std::to_string(g_registry_selected + 1) + "/" +
                     std::to_string(g_registry_entries.size()), 250, 45, 42, 12,
                     &lv_font_montserrat_10, 0x8B949E, LV_LABEL_LONG_DOT);
        uint32_t nav_color = g_registry_page_focus == 1 ? 0xFF6A3D : 0x6E7681;
        strong_label(g_root, "<", 8, 88, 12, 18, &lv_font_montserrat_20, nav_color);
        strong_label(g_root, ">", 303, 88, 12, 18, &lv_font_montserrat_20, nav_color);
        label(g_root, "NAME", 24, 62, 34, 12, &lv_font_montserrat_10,
              g_registry_page_focus == 1 ? 0xCCCC33 : 0x58A6FF);
        strong_label(g_root, one_line(entry.name, 28), 24, 75, 210, 16,
                     &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
        label(g_root, entry.enabled ? "on" : "off", 242, 75, 28, 12, &lv_font_montserrat_10,
              entry.enabled ? 0x43CF4D : 0x6E7681, LV_LABEL_LONG_DOT);
        label(g_root, entry.count + " apps", 272, 75, 42, 12, &lv_font_montserrat_10,
              0xCCCC33, LV_LABEL_LONG_DOT);
        label(g_root, entry.builtin ? "URL (region)" : "URL", 24, 96, 80, 12,
              &lv_font_montserrat_10, 0x58A6FF);
        box(24, 109, 272, 26, 0x111923, 0x2A3A46, 1, 2);
        std::string display_url = entry.builtin ? g_region_registry_url : entry.url;
        label(g_root, display_url, 30, 112, 260, 20, &lv_font_montserrat_10, 0xE6EDF3,
              LV_LABEL_LONG_WRAP);
        if (!entry.error.empty()) {
            label(g_root, one_line(entry.error, 45), 24, 136, 272, 12, &lv_font_montserrat_10,
                  0xF85149, LV_LABEL_LONG_DOT);
        }
    }

    bool op_running = registry_op_is_running();
    std::string hint = g_region_commit_pending ? "Region update pending..." :
        (op_running ? "Registry operation running..." : (region_focused ?
        "A add registry  Up/Down focus  < > region" :
        "A add registry  E edit  T toggle  D del  R sync"));
    label(g_root, hint, 10, 153, 300, 12, &lv_font_montserrat_10,
          0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_registry_edit()
{
    TraceScope trace("render_registry_edit", true);
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, g_registry_edit_url.empty() ? "Add Registry" : "Edit Registry",
          8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);

    label(g_root, "NAME", 10, 46, 50, 12, &lv_font_montserrat_10,
          g_registry_focus == 0 ? 0xCCCC33 : 0x58A6FF);
    box(10, 59, 300, 24, 0x111923, g_registry_focus == 0 ? 0xCCCC33 : 0x2A3A46, 1, 2);
    label(g_root, one_line(g_registry_name_input, 34), 16, 64, 288, 14,
          &lv_font_montserrat_12, 0xE6EDF3, LV_LABEL_LONG_DOT);

    label(g_root, "URL", 10, 88, 50, 12, &lv_font_montserrat_10,
          g_registry_focus == 1 ? 0xCCCC33 : 0x58A6FF);
    box(10, 101, 300, 42, 0x111923, g_registry_focus == 1 ? 0xCCCC33 : 0x2A3A46, 1, 2);
    label(g_root, g_registry_input, 16, 105, 288, 34, &lv_font_montserrat_12,
          0xE6EDF3, LV_LABEL_LONG_WRAP);

    label(g_root, "Up/Down focus  Enter save", 10, 153, 166, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    label(g_root, "Backspace del  Esc back", 176, 153, 134, 12,
          &lv_font_montserrat_10, 0x8B949E, LV_LABEL_LONG_DOT);
}

void render_share_code()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Share Code", 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);

    center_strong_label(g_root, "TYPE SHARE CODE", 68, 51, 184, 16,
                        &lv_font_montserrat_12, 0x58A6FF, LV_LABEL_LONG_DOT);
    box(68, 69, 184, 45, 0x111923, 0x58A6FF, 2, 3);
    std::string display = g_share_code_input.empty() ? "share code" : upper_ascii(g_share_code_input);
    center_strong_label(g_root, one_line(display, 12), 78, 81, 164, 23,
                        &lv_font_montserrat_20,
                        g_share_code_input.empty() ? 0x6E7681 : 0xE6EDF3,
                        LV_LABEL_LONG_DOT);

    center_label(g_root, one_line(g_share_code_message, 48), 10, 122, 300, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    label(g_root, "Enter Open   Backspace Delete   Esc Back", 10, 153, 300, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_search()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Search", 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "Esc Back", 258, 24, 56, 14, &lv_font_montserrat_10, 0xAECBFA);

    if (g_search_results_active && !g_search_results.empty()) {
        label(g_root, "QUERY", 10, 48, 42, 12, &lv_font_montserrat_10, 0x7ED957);
        box(54, 44, 116, 20, 0x111923, 0x2EA043, 1, 2);
        label(g_root, one_line(upper_ascii(g_search_input), 12), 60, 49, 104, 12,
              &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
        std::string count = std::to_string(g_search_results.size()) + " RESULTS";
        label(g_root, count, 182, 48, 128, 12, &lv_font_montserrat_10, 0xB8B8B8,
              LV_LABEL_LONG_DOT);

        int total = static_cast<int>(g_search_results.size());
        if (g_search_selected >= total) g_search_selected = total - 1;
        if (g_search_selected < 0) g_search_selected = 0;
        int first = std::max(0, g_search_selected - 1);
        int last = std::min(total - 1, first + 3);
        first = std::max(0, last - 3);

        for (int result_index = first; result_index <= last; ++result_index) {
            int row = result_index - first;
            int y = 68 + row * 18;
            bool selected = result_index == g_search_selected;
            const StoreApp &app = g_apps[g_search_results[result_index]];
            if (selected) {
                box(10, y - 2, 300, 17, 0x142817, 0x7ED957, 1, 2);
            }
            std::string name = one_line(upper_ascii(app.name), 18);
            strong_label(g_root, name, 16, y, 138, 13,
                         font_for_text(name, selected ? &lv_font_montserrat_12 : &lv_font_montserrat_10),
                         selected ? 0xFFFFFF : 0xB8B8B8, LV_LABEL_LONG_DOT);
            std::string meta = one_line(app.category.empty() ? app.author : app.category, 14);
            label(g_root, meta, 164, y + 1, 88, 12, &lv_font_montserrat_10,
                  selected ? 0x7ED957 : 0x8B949E, LV_LABEL_LONG_DOT);
            label(g_root, "V" + one_line(app.version.empty() ? "0" : app.version, 5),
                  260, y + 1, 48, 12, &lv_font_montserrat_10, 0x8B8B8B, LV_LABEL_LONG_DOT);
        }

        label(g_root, "Up/Down Select   Enter Open   Backspace Edit",
              10, 153, 300, 12, &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
        return;
    }

    center_strong_label(g_root, "SEARCH APPS", 68, 51, 184, 16,
                        &lv_font_montserrat_12, 0x7ED957, LV_LABEL_LONG_DOT);
    box(50, 69, 220, 45, 0x111923, 0x7ED957, 2, 3);
    std::string display = g_search_input.empty() ? "app name" : upper_ascii(g_search_input);
    center_strong_label(g_root, one_line(display, 16), 60, 81, 200, 23,
                        &lv_font_montserrat_20,
                        g_search_input.empty() ? 0x6E7681 : 0xE6EDF3,
                        LV_LABEL_LONG_DOT);

    center_label(g_root, one_line(g_search_message, 48), 10, 122, 300, 14,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    label(g_root, "Enter Open   Backspace Delete   Esc Back", 10, 153, 300, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_screenshots()
{
    clean_root();
    box(0, 0, 320, 170, 0x05070A, 0x05070A, 0);
    StoreApp *app = ensure_selected_app();
    if (!app) {
        box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
        label(g_root, "No selected app", 10, 50, 304, 14, &lv_font_montserrat_12, 0xE6EDF3);
        return;
    }
    std::vector<std::string> screenshots = detail_screenshot_paths(g_app_dir, *app);
    normalize_detail_image_state(*app, screenshots);
    if (!screenshots.empty()) {
        draw_detail_background(*app);
    }

    if (screenshots.empty()) {
        box(0, 0, 320, 23, 0x05070A, 0x05070A, 0, 0, static_cast<lv_opa_t>(210));
        box(0, 145, 320, 25, 0x05070A, 0x05070A, 0, 0, static_cast<lv_opa_t>(210));
        label(g_root, "Screenshots", 8, 5, 120, 14, &lv_font_montserrat_12, 0xFFFFFF);
        center_strong_label(g_root, "NO SCREENSHOTS", 52, 76, 216, 16,
                            &lv_font_montserrat_14, 0xCCCC33, LV_LABEL_LONG_DOT);
        label(g_root, "Esc Back", 258, 5, 56, 14, &lv_font_montserrat_10, 0xAECBFA);
        return;
    }

    if (g_screenshots_overlay_visible && lv_tick_elaps(g_screenshots_activity_tick) >= 2000) {
        g_screenshots_overlay_visible = false;
    }
    if (!g_screenshots_overlay_visible) return;

    box(0, 0, 320, 23, 0x05070A, 0x05070A, 0, 0, static_cast<lv_opa_t>(210));
    box(0, 145, 320, 25, 0x05070A, 0x05070A, 0, 0, static_cast<lv_opa_t>(210));
    label(g_root, "Screenshots", 8, 5, 120, 14, &lv_font_montserrat_12, 0xFFFFFF);
    std::string count = std::to_string(g_detail_image_index + 1) + "/" +
                        std::to_string(screenshots.size());
    center_strong_label(g_root, count, 136, 5, 48, 14,
                        &lv_font_montserrat_10, 0xAECBFA, LV_LABEL_LONG_DOT);
    label(g_root, "Esc Back", 258, 5, 56, 14, &lv_font_montserrat_10, 0xAECBFA);
    center_label(g_root, "Z / < previous        C / > next", 36, 153, 248, 12,
                 &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render()
{
    switch (g_screen) {
        case Screen::StartupSync: render_startup_sync(); break;
        case Screen::Home: render_home(); break;
        case Screen::Detail: render_detail(); break;
        case Screen::Confirm: render_confirm(); break;
        case Screen::SudoPassword: render_sudo_password(); break;
        case Screen::Progress: render_progress(); break;
        case Screen::ErrorDialog: render_error_dialog(); break;
        case Screen::Registry: render_registry(); break;
        case Screen::RegistryEdit: render_registry_edit(); break;
        case Screen::ShareCode: render_share_code(); break;
        case Screen::Search: render_search(); break;
        case Screen::Screenshots: render_screenshots(); break;
    }
}

bool allow_fast_top_status_render()
{
    return g_screen != Screen::Registry && g_screen != Screen::RegistryEdit;
}

bool allow_sync_anim_render()
{
    return g_screen != Screen::Registry && g_screen != Screen::RegistryEdit;
}

void refresh_timer_cb(lv_timer_t *)
{
    uint32_t start = lv_tick_get();
    bool region_debounce = poll_region_debounce();
    if (region_debounce) {
        render();
    }
    bool registry_refresh_done = poll_registry_refresh();
    if (registry_refresh_done) {
        render();
    }
    bool registry_op_done = poll_registry_op();
    if (registry_op_done) {
        render();
    }
    bool plan_done = poll_plan_check();
    if (plan_done) {
        render();
    }
    bool summary_done = poll_summary_refresh();
    if (summary_done) {
        render();
    }
    bool status_timeout = false;
    if (!g_status_message.empty() && g_status_scroll_start_tick != 0 &&
        lv_tick_elaps(g_status_scroll_start_tick) >= g_status_scroll_hide_after_ms) {
        g_status_message.clear();
        g_status_scroll_text.clear();
        g_status_scroll_start_tick = 0;
        g_status_scroll_hide_after_ms = 0;
        status_timeout = true;
        render();
    }
    bool battery_charging = g_top_battery_status.valid && (g_top_battery_status.flags & 1);
    bool fast_top_status = battery_charging && allow_fast_top_status_render();
    bool top_status_render = false;
    if ((fast_top_status &&
         lv_tick_elaps(g_top_status_last_render_tick) >= kBatteryChargeAnimRefreshMs) ||
        (!fast_top_status && g_top_status_tick != 0 &&
         lv_tick_elaps(g_top_status_tick) >= kTopStatusRefreshMs)) {
        top_status_render = true;
        render();
    }
    bool screenshots_hide = false;
    if (g_screen == Screen::Screenshots && g_screenshots_overlay_visible &&
        lv_tick_elaps(g_screenshots_activity_tick) >= 2000) {
        g_screenshots_overlay_visible = false;
        screenshots_hide = true;
        render();
    }
    uint32_t elapsed = lv_tick_elaps(start);
    if (elapsed >= 20 || region_debounce || registry_refresh_done || registry_op_done || plan_done ||
        summary_done || status_timeout || top_status_render || screenshots_hide ||
        g_screen == Screen::Registry || g_screen == Screen::RegistryEdit) {
        app_tracef("refresh_timer elapsed=%ums screen=%s region_debounce=%d registry_refresh=%d registry_op=%d plan=%d summary=%d status_timeout=%d top_status=%d screenshots=%d",
                   elapsed, screen_name(g_screen), region_debounce ? 1 : 0, registry_refresh_done ? 1 : 0,
                   registry_op_done ? 1 : 0, plan_done ? 1 : 0, summary_done ? 1 : 0,
                   status_timeout ? 1 : 0, top_status_render ? 1 : 0,
                   screenshots_hide ? 1 : 0);
    }
}

void finish_backend_job(const std::string &out, const std::string &rc_text)
{
    bool ok = trim(rc_text) == "0" && out.find("ERROR") == std::string::npos;
    std::string finished_action = g_job_action;
    std::string finished_title = g_job_title.empty() ? "Selected app" : g_job_title;
    app_tracef("job finish action=%s app=%s rc=%s ok=%d output=%s",
               g_job_action.c_str(), g_job_app_id.c_str(), rc_text.c_str(), ok ? 1 : 0,
               one_line(out, 220).c_str());
    update_local_job_app_state(ok, out);
    if (!ok) {
        g_error_title = upper_ascii(job_action_label(finished_action)) + " FAILED";
        g_error_message = one_line(finished_title, 36);
        g_error_detail = backend_error_message(out);
        if (g_error_detail.empty()) g_error_detail = "Package operation failed.";
        g_status_message.clear();
    } else if (g_job_action == "uninstall" || out.find("UNINSTALLED") != std::string::npos) {
        g_status_message = "Deleted";
    } else if (g_job_action == "upgrade" || out.find("UPGRADED") != std::string::npos) {
        g_status_message = "Upgraded. Return to launcher to test.";
    } else if (out.find("INSTALLED") != std::string::npos) {
        g_status_message = "Installed. Return to launcher to test.";
    } else {
        g_status_message = "Done";
    }
    g_job_running = false;
    g_job_pending_start = false;
    g_job_app_id.clear();
    g_job_progress = -1;
    g_job_stage.clear();
    g_job_detail.clear();
    g_job_output.clear();
    g_job_sudo_password.clear();
    g_job_rc = -1;
    g_job_done = false;
    request_summary_refresh();
    g_screen = ok ? Screen::Detail : Screen::ErrorDialog;
}

void poll_backend_job()
{
    if (!g_job_running || g_job_pending_start) return;
    uint32_t elapsed = lv_tick_elaps(g_job_start_tick) / 1000;
    std::string out;
    int rc = -1;
    bool done = false;
    pthread_mutex_lock(&g_job_mutex);
    out = g_job_output;
    rc = g_job_rc;
    done = g_job_done;
    pthread_mutex_unlock(&g_job_mutex);
    parse_job_progress(out);
    std::string detail = g_job_detail.empty() ? job_action_label(g_job_action) : g_job_detail;
    if (g_job_progress >= 0) {
        detail += " " + std::to_string(g_job_progress) + "%";
    }
    g_status_message = detail + " " + one_line(g_job_title, 16) + " " + std::to_string(elapsed) + "s";
    if (!done) return;
    finish_backend_job(out, std::to_string(rc));
}

void *backend_job_thread_main(void *)
{
    std::string flag = "--install";
    if (g_job_action == "reinstall") flag = "--reinstall";
    else if (g_job_action == "upgrade") flag = "--upgrade";
    else if (g_job_action == "uninstall") flag = "--uninstall";

    int rc = -1;
    app_tracef("job backend start action=%s app=%s flag=%s",
               g_job_action.c_str(), g_job_app_id.c_str(), flag.c_str());
    std::string out = backend_capture_with_sudo({flag, g_job_app_id}, g_job_sudo_password, &rc);
    app_tracef("job backend done action=%s app=%s rc=%d bytes=%zu",
               g_job_action.c_str(), g_job_app_id.c_str(), rc, out.size());
    pthread_mutex_lock(&g_job_mutex);
    g_job_output = out;
    g_job_rc = rc;
    g_job_done = true;
    pthread_mutex_unlock(&g_job_mutex);
    return nullptr;
}

void job_timer_cb(lv_timer_t *)
{
    if (!g_job_running && !g_job_pending_start) return;
    if (g_job_pending_start && lv_tick_elaps(g_job_start_tick) >= kJobStartDelayMs) {
        g_job_pending_start = false;
        pthread_mutex_lock(&g_job_mutex);
        g_job_output.clear();
        g_job_rc = -1;
        g_job_done = false;
        pthread_mutex_unlock(&g_job_mutex);

        if (pthread_create(&g_job_thread, nullptr, backend_job_thread_main, nullptr) != 0) {
            g_job_running = false;
            g_status_message = "Unable to start operation";
            g_screen = Screen::Detail;
            render();
            return;
        }
        pthread_detach(g_job_thread);
        g_job_start_tick = lv_tick_get();
        g_job_detail = "Waiting for package output";
        g_status_message = job_action_label(g_job_action) + " " + one_line(g_job_title, 18) + "... 0s";
    }
    poll_backend_job();
    render();
}

void update_local_job_app_state(bool ok, const std::string &out)
{
    if (!ok || g_job_app_id.empty()) return;
    for (StoreApp &app : g_apps) {
        if (app.id != g_job_app_id) continue;
        if (g_job_action == "uninstall" || out.find("UNINSTALLED") != std::string::npos) {
            app.installed = false;
            app.installed_version.clear();
        } else if (out.find("INSTALLED") != std::string::npos ||
                   out.find("UPGRADED") != std::string::npos ||
                   g_job_action == "install" || g_job_action == "reinstall" || g_job_action == "upgrade") {
            app.installed = true;
            if (app.installed_version.empty()) app.installed_version = app.version;
        }
        app_tracef("job local_state app=%s action=%s installed=%d version=%s out=%s",
                   app.id.c_str(), g_job_action.c_str(), app.installed ? 1 : 0,
                   app.installed_version.empty() ? "-" : app.installed_version.c_str(),
                   one_line(out, 140).c_str());
        break;
    }
}

void navigate_back()
{
    switch (g_screen) {
        case Screen::StartupSync:
            request_quit();
            break;
        case Screen::Home:
            break;
        case Screen::Detail:
            g_screen = Screen::Home;
            break;
        case Screen::Confirm:
            g_screen = Screen::Detail;
            g_confirm_action.clear();
            g_confirm_lines.clear();
            g_confirm_focus = 0;
            break;
        case Screen::SudoPassword:
            g_screen = Screen::Confirm;
            g_sudo_password_input.clear();
            g_sudo_message = "Enter sudo password for package operation.";
            break;
        case Screen::Progress:
            if (g_job_running || g_job_pending_start) {
                g_status_message = "Operation is still running";
            } else {
                g_screen = Screen::Detail;
            }
            break;
        case Screen::ErrorDialog:
            g_error_title.clear();
            g_error_message.clear();
            g_error_detail.clear();
            g_screen = Screen::Detail;
            break;
        case Screen::Registry:
            g_screen = Screen::Home;
            break;
        case Screen::RegistryEdit:
            g_screen = Screen::Registry;
            break;
        case Screen::ShareCode:
            g_screen = Screen::Home;
            break;
        case Screen::Search:
            g_screen = Screen::Home;
            break;
        case Screen::Screenshots:
            g_screen = Screen::Detail;
            break;
    }
}

void esc_hold_timer_cb(lv_timer_t *)
{
    if (!g_esc_pressed || g_esc_long_consumed) return;
    if (lv_tick_elaps(g_esc_press_tick) >= kEscLongPressMs) {
        g_esc_long_consumed = true;
        request_quit();
    }
}

void apply_sync_output(const std::string &out, bool refresh_registries_after)
{
    std::fprintf(stderr, "[AppStore UI] apply_sync_output bytes=%zu refresh_registries=%d preview=%s\n",
                 out.size(), refresh_registries_after ? 1 : 0, one_line(out, 180).c_str());
    std::string message = sync_status_message(out);
    if (!message.empty()) {
        g_status_message = one_line(message, 54);
    } else if (out.find("SYNC\t0") != std::string::npos) {
        g_status_message = "No apps loaded";
    } else {
        g_status_message.clear();
    }
    request_summary_refresh();
    if (refresh_registries_after) request_registry_refresh();
}

void *sync_thread_main(void *arg)
{
    SyncRequest request = *static_cast<SyncRequest *>(arg);
    delete static_cast<SyncRequest *>(arg);
    std::fprintf(stderr, "[AppStore UI] sync thread start\n");
    app_tracef("sync worker begin gen=%llu", static_cast<unsigned long long>(request.generation));
    std::string out = backend_capture({"--sync"});
    std::fprintf(stderr, "[AppStore UI] sync thread backend returned bytes=%zu preview=%s\n",
                 out.size(), one_line(out, 180).c_str());
    app_tracef("sync worker end gen=%llu bytes=%zu",
               static_cast<unsigned long long>(request.generation), out.size());
    pthread_mutex_lock(&g_sync_mutex);
    if (request.generation == g_sync_generation) {
        g_sync_output = out;
        g_sync_done = true;
        g_sync_running = false;
    } else {
        app_tracef("sync worker stale gen=%llu current=%llu",
                   static_cast<unsigned long long>(request.generation),
                   static_cast<unsigned long long>(g_sync_generation));
    }
    pthread_mutex_unlock(&g_sync_mutex);
    return nullptr;
}

void sync_catalog(bool refresh_registries_after)
{
    std::fprintf(stderr, "[AppStore UI] sync_catalog request refresh_registries=%d\n",
                 refresh_registries_after ? 1 : 0);
    pthread_mutex_lock(&g_sync_mutex);
    if (g_sync_running) {
        pthread_mutex_unlock(&g_sync_mutex);
        g_status_message = "Sync already running";
        std::fprintf(stderr, "[AppStore UI] sync_catalog ignored: already running\n");
        return;
    }
    g_sync_running = true;
    g_sync_done = false;
    g_sync_refresh_registries = refresh_registries_after;
    g_sync_output.clear();
    g_sync_anim_phase = -1;
    g_sync_visible_until_tick = lv_tick_get();
    uint64_t generation = ++g_sync_generation;
    pthread_mutex_unlock(&g_sync_mutex);

    g_status_message = "Syncing catalog...";
    pthread_t thread_id;
    auto *thread_arg = new SyncRequest{generation};
    if (pthread_create(&thread_id, nullptr, sync_thread_main, thread_arg) != 0) {
        delete thread_arg;
        pthread_mutex_lock(&g_sync_mutex);
        g_sync_running = false;
        g_sync_done = false;
        g_sync_visible_until_tick = 0;
        pthread_mutex_unlock(&g_sync_mutex);
        g_status_message = "Unable to start sync";
        std::fprintf(stderr, "[AppStore UI] sync_catalog failed: pthread_create\n");
        return;
    }
    pthread_detach(thread_id);
    std::fprintf(stderr, "[AppStore UI] sync_catalog thread started\n");
}

void sync_timer_cb(lv_timer_t *)
{
    std::string out;
    bool done = false;
    bool running = false;
    bool refresh_registries_after = false;
    pthread_mutex_lock(&g_sync_mutex);
    running = g_sync_running;
    if (g_sync_done) {
        done = true;
        out = g_sync_output;
        refresh_registries_after = g_sync_refresh_registries;
        g_sync_done = false;
        g_sync_output.clear();
        g_sync_refresh_registries = false;
    }
    pthread_mutex_unlock(&g_sync_mutex);
    if (running) {
        if (g_startup_sync_active || g_screen == Screen::StartupSync) {
            SyncStatus status = load_sync_status();
            bool changed = status.running != g_sync_status.running ||
                status.cancel_requested != g_sync_status.cancel_requested ||
                status.url != g_sync_status.url ||
                status.detail != g_sync_status.detail ||
                status.percent != g_sync_status.percent ||
                status.phase != g_sync_status.phase;
            g_sync_status = std::move(status);
            if (changed && g_screen == Screen::StartupSync) {
                render();
            }
        }
        int phase = static_cast<int>((lv_tick_get() / kSyncAnimRefreshMs) % 4);
        if (phase != g_sync_anim_phase) {
            g_sync_anim_phase = phase;
            if (allow_sync_anim_render()) {
                app_tracef("sync_timer anim render phase=%d screen=%s", phase, screen_name(g_screen));
                render();
            } else {
                app_tracef("sync_timer anim skip phase=%d screen=%s", phase, screen_name(g_screen));
            }
        }
        return;
    }
    if (!done) return;
    std::fprintf(stderr, "[AppStore UI] sync_timer done bytes=%zu\n", out.size());
    g_sync_anim_phase = -1;
    if (g_startup_sync_active) {
        g_startup_sync_active = false;
        std::string startup_error;
        if (sync_output_failed_for_startup(out, &startup_error)) {
            g_startup_sync_failed = true;
            g_sync_status.running = false;
            g_sync_status.percent = -1;
            g_sync_status.phase = "network";
            g_sync_status.detail = "Network sync failed";
            g_error_title = "NETWORK FAILED";
            g_error_message = "Catalog sync failed";
            g_error_detail = startup_error.empty() ? "Check Wi-Fi, then open AppStore again." : startup_error;
            g_status_message.clear();
            render();
            return;
        }
        apply_sync_output(out, refresh_registries_after);
        if (g_screen == Screen::StartupSync) {
            g_screen = Screen::Home;
        }
    } else {
        apply_sync_output(out, refresh_registries_after);
    }
    render();
}

void cancel_startup_sync_and_open_registry()
{
    if (g_startup_sync_active) {
        g_startup_sync_cancelled = true;
        cancel_sync();
        pthread_mutex_lock(&g_sync_mutex);
        ++g_sync_generation;
        g_sync_running = false;
        g_sync_done = false;
        g_sync_output.clear();
        g_sync_refresh_registries = false;
        pthread_mutex_unlock(&g_sync_mutex);
        g_startup_sync_active = false;
        g_sync_status.cancel_requested = true;
        g_sync_status.detail = "Cancelling sync...";
        g_status_message = "Startup sync cancelled";
    }
    open_registry_screen();
}

void open_registry_screen()
{
    app_tracef("open_registry_screen begin from=%s entries=%zu loading=%d refresh_running=%d",
               screen_name(g_screen), g_registry_entries.size(), g_registry_loading ? 1 : 0,
               g_registry_refresh_running ? 1 : 0);
    g_screen = Screen::Registry;
    g_status_message = "Loading registries...";
    request_registry_refresh();
    app_tracef("open_registry_screen end screen=%s", screen_name(g_screen));
}

void open_registry_add_screen()
{
    if (!registry_op_available()) return;
    g_registry_edit_url.clear();
    g_registry_name_input.clear();
    g_registry_input = kDefaultRegistryUrl;
    g_registry_focus = 0;
    g_status_message.clear();
    g_screen = Screen::RegistryEdit;
}

void open_share_code_screen()
{
    g_share_code_input.clear();
    g_share_code_message = "Enter a code from CardputerZero Hub.";
    g_share_code_open_tick = lv_tick_get();
    g_screen = Screen::ShareCode;
}

void clear_search_results()
{
    g_search_results.clear();
    g_search_selected = 0;
    g_search_results_active = false;
}

void open_search_screen()
{
    g_search_input.clear();
    g_search_message = "Search by app name, author, category, or code.";
    clear_search_results();
    g_screen = Screen::Search;
}

void open_screenshots_screen()
{
    StoreApp *app = ensure_selected_app();
    if (!app) {
        g_status_message = "No selected app";
        return;
    }
    std::vector<std::string> screenshots = detail_screenshot_paths(g_app_dir, *app);
    normalize_detail_image_state(*app, screenshots);
    if (screenshots.empty()) {
        g_status_message = "No screenshots for this app";
        return;
    }
    g_status_message.clear();
    show_screenshots_overlay();
    g_screen = Screen::Screenshots;
}

bool select_app_index(int app_index)
{
    if (app_index < 0 || app_index >= static_cast<int>(g_apps.size())) return false;
    for (int i = 0; i < static_cast<int>(g_categories.size()); ++i) {
        if (g_categories[i] == "All") {
            g_category = i;
            break;
        }
    }
    rebuild_visible();
    for (int i = 0; i < static_cast<int>(g_visible.size()); ++i) {
        if (g_visible[i] == app_index) {
            g_selected = i;
            return true;
        }
    }
    return false;
}

void cycle_sort_rule()
{
    StoreApp *app = selected_app();
    std::string selected_id = app ? app->id : "";
    switch (g_sort_rule) {
        case SortRule::Default: g_sort_rule = SortRule::New; break;
        case SortRule::New: g_sort_rule = SortRule::Old; break;
        case SortRule::Old: g_sort_rule = SortRule::AtoZ; break;
        case SortRule::AtoZ: g_sort_rule = SortRule::ZtoA; break;
        case SortRule::ZtoA: g_sort_rule = SortRule::Default; break;
    }
    sort_apps(g_apps, g_sort_rule);
    rebuild_visible();
    if (!select_visible_app_by_id(selected_id)) g_selected = 0;
    g_status_message.clear();
}

void open_share_code_match()
{
    std::string code = match_key(g_share_code_input);
    if (code.empty()) {
        g_share_code_message = "Type a share code first.";
        return;
    }
    if (g_apps.empty()) {
        g_share_code_message = "Catalog is still loading.";
        request_summary_refresh();
        return;
    }
    for (int i = 0; i < static_cast<int>(g_apps.size()); ++i) {
        const StoreApp &app = g_apps[i];
        if (match_key(app.share_code) == code || match_key(app.id) == code ||
            match_key(app.name) == code) {
            if (select_app_index(i)) {
                g_status_message.clear();
                g_share_code_message.clear();
                g_screen = Screen::Detail;
                return;
            }
        }
    }
    g_share_code_message = "No app found for code: " + g_share_code_input;
}

void open_search_match()
{
    std::string query = match_key(g_search_input);
    if (query.empty()) {
        g_search_message = "Type a search term first.";
        clear_search_results();
        return;
    }
    if (g_apps.empty()) {
        clear_search_results();
        g_search_message = "Catalog is still loading.";
        request_summary_refresh();
        return;
    }
    g_search_results.clear();
    for (int i = 0; i < static_cast<int>(g_apps.size()); ++i) {
        const StoreApp &app = g_apps[i];
        std::string haystack = match_key(app.name + " " + app.author + " " + app.category +
                                         " " + app.share_code + " " + app.id);
        if (haystack.find(query) != std::string::npos) {
            g_search_results.push_back(i);
        }
    }
    if (g_search_results.empty()) {
        clear_search_results();
        g_search_message = "No app found for: " + g_search_input;
        return;
    }
    if (g_search_results.size() == 1) {
        int app_index = g_search_results.front();
        clear_search_results();
        if (select_app_index(app_index)) {
            g_status_message.clear();
            g_search_message.clear();
            g_screen = Screen::Detail;
            return;
        }
        g_search_message = "Unable to open result.";
        return;
    }
    g_search_selected = 0;
    g_search_results_active = true;
    g_search_message = std::to_string(g_search_results.size()) + " apps found. Select one.";
}

void open_selected_search_result()
{
    if (!g_search_results_active || g_search_results.empty()) {
        open_search_match();
        return;
    }
    if (g_search_selected < 0) g_search_selected = 0;
    if (g_search_selected >= static_cast<int>(g_search_results.size())) {
        g_search_selected = static_cast<int>(g_search_results.size()) - 1;
    }
    if (select_app_index(g_search_results[g_search_selected])) {
        clear_search_results();
        g_status_message.clear();
        g_search_message.clear();
        g_screen = Screen::Detail;
    }
}

RegistryEntry *selected_registry()
{
    if (g_registry_selected < 0 || g_registry_selected >= static_cast<int>(g_registry_entries.size())) return nullptr;
    return &g_registry_entries[g_registry_selected];
}

std::string region_label_for_code(const std::string &region)
{
    if (region == "CN") return "China";
    if (region == "default") return "Default";
    return "Auto";
}

std::string region_registry_url_for_code(const std::string &region)
{
    if (region == "CN") {
        return "https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json";
    }
    if (region == "default") return kDefaultRegistryUrl;
    if (g_region_active == "CN") {
        return "https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json";
    }
    return kDefaultRegistryUrl;
}

void apply_local_region_selection(const std::string &region)
{
    g_region_code = region;
    g_region_label = region_label_for_code(region);
    g_region_registry_url = region_registry_url_for_code(region);
    if (region != "auto") g_region_active = region;
    for (RegistryEntry &entry : g_registry_entries) {
        if (entry.builtin || entry.region == "CN" || entry.region == "default") {
            entry.url = g_region_registry_url;
            entry.region = region == "auto" ? g_region_active : region;
            if (entry.name.empty() || entry.name == entry.url) {
                entry.name = "CardputerZero Hub";
            }
        }
    }
}

void start_debounced_region_commit()
{
    if (!g_region_commit_pending) return;
    if (registry_op_is_running()) return;

    std::string region = g_region_pending_code;
    g_region_commit_pending = false;
    RegistryOpRequest request;
    request.kind = RegistryOpKind::SetRegion;
    request.region = region;
    start_registry_op(request, "Changing region...");
}

bool poll_region_debounce()
{
    if (!g_region_commit_pending) return false;
    if (lv_tick_elaps(g_region_change_tick) < kRegionDebounceMs) return false;
    start_debounced_region_commit();
    return true;
}

void select_region(const std::string &region)
{
    if (region == g_region_code) {
        return;
    }
    cancel_registry_online_work("region_input");
    apply_local_region_selection(region);
    g_region_pending_code = region;
    g_region_commit_pending = true;
    g_region_change_tick = lv_tick_get();
    g_status_message = "Region: " + g_region_label + " (waiting...)";
    app_tracef("region debounce select region=%s label=%s", region.c_str(), g_region_label.c_str());
}

std::string adjacent_region(int delta)
{
    static const std::vector<std::string> regions = {"auto", "default", "CN"};
    auto it = std::find(regions.begin(), regions.end(), g_region_code);
    int index = it == regions.end() ? 0 : static_cast<int>(it - regions.begin());
    int next = (index + delta) % static_cast<int>(regions.size());
    if (next < 0) next += static_cast<int>(regions.size());
    return regions[next];
}

void add_registry_from_input()
{
    if (g_registry_name_input.empty() || g_registry_input.empty()) {
        g_status_message = "Name and URL required";
        return;
    }
    if (!registry_op_available()) return;
    bool editing = !g_registry_edit_url.empty();
    RegistryOpRequest request;
    request.kind = editing ? RegistryOpKind::EditRegistry : RegistryOpKind::AddRegistry;
    request.old_url = g_registry_edit_url;
    request.url = g_registry_input;
    request.name = g_registry_name_input;
    start_registry_op(request, editing ? "Updating registry..." : "Adding registry...");
    render();
}

void toggle_selected_registry()
{
    if (!registry_op_available()) return;
    RegistryEntry *entry = selected_registry();
    if (!entry) return;
    RegistryOpRequest request;
    request.kind = RegistryOpKind::ToggleRegistry;
    request.url = entry->url;
    request.enable = !entry->enabled;
    start_registry_op(request, entry->enabled ? "Disabling registry..." : "Enabling registry...");
}

void delete_selected_registry()
{
    if (!registry_op_available()) return;
    RegistryEntry *entry = selected_registry();
    if (!entry) return;
    if (entry->builtin) {
        g_status_message = "Use region setting";
        return;
    }
    RegistryOpRequest request;
    request.kind = RegistryOpKind::DeleteRegistry;
    request.url = entry->url;
    start_registry_op(request, "Deleting registry...");
}

void edit_selected_registry()
{
    if (!registry_op_available()) return;
    RegistryEntry *entry = selected_registry();
    if (!entry) return;
    if (entry->builtin) {
        g_status_message = "Use region setting";
        return;
    }
    g_registry_edit_url = entry->url;
    g_registry_input = entry->url;
    g_registry_name_input = entry->name;
    g_registry_focus = 0;
    g_status_message.clear();
    g_screen = Screen::RegistryEdit;
}

bool parse_plan(const std::string &out)
{
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.size() >= 8 && fields[0] == "PLAN") {
            g_confirm_lines.clear();
            std::string verb = "Install ";
            if (g_confirm_action == "uninstall") verb = "Delete ";
            else if (g_confirm_action == "upgrade") verb = "Upgrade ";
            else if (g_confirm_action == "reinstall") verb = "Reinstall ";
            g_confirm_lines.push_back(verb + fields[2]);
            g_confirm_lines.push_back("Download/app size: " + fields[4]);
            g_confirm_lines.push_back("Disk free: " + fields[5]);
            g_confirm_lines.push_back("Dependencies: " + (fields[6].empty() ? "-" : fields[6]));
            if (has_blocking_missing(fields[7])) {
                g_status_message = missing_install_message(fields[7]);
                return false;
            }
            g_confirm_lines.push_back("Install checks: " + (fields[7].empty() ? "OK" : fields[7]));
            return true;
        }
    }
    return false;
}

void *plan_thread_main(void *)
{
    std::string app_id;
    pthread_mutex_lock(&g_plan_mutex);
    app_id = g_plan_app_id;
    pthread_mutex_unlock(&g_plan_mutex);

    int rc = -1;
    std::string out = backend_capture({"--plan", app_id}, &rc);
    pthread_mutex_lock(&g_plan_mutex);
    g_plan_output = out;
    g_plan_rc = rc;
    g_plan_done = true;
    pthread_mutex_unlock(&g_plan_mutex);
    return nullptr;
}

void start_plan_check(const std::string &action, const std::string &app_id)
{
    pthread_mutex_lock(&g_plan_mutex);
    if (g_plan_running) {
        pthread_mutex_unlock(&g_plan_mutex);
        g_status_message = "Plan check already running";
        return;
    }
    g_plan_running = true;
    g_plan_done = false;
    g_plan_action = action;
    g_plan_app_id = app_id;
    g_plan_output.clear();
    g_plan_rc = -1;
    pthread_mutex_unlock(&g_plan_mutex);

    g_status_message = "Checking install plan...";
    pthread_t thread_id;
    if (pthread_create(&thread_id, nullptr, plan_thread_main, nullptr) != 0) {
        pthread_mutex_lock(&g_plan_mutex);
        g_plan_running = false;
        pthread_mutex_unlock(&g_plan_mutex);
        g_status_message = "Unable to check install plan";
        return;
    }
    pthread_detach(thread_id);
}

bool poll_plan_check()
{
    std::string action;
    std::string out;
    int rc = -1;
    bool done = false;

    pthread_mutex_lock(&g_plan_mutex);
    if (g_plan_done) {
        done = true;
        action = g_plan_action;
        out = std::move(g_plan_output);
        rc = g_plan_rc;
        g_plan_done = false;
        g_plan_running = false;
        g_plan_action.clear();
        g_plan_app_id.clear();
        g_plan_rc = -1;
    }
    pthread_mutex_unlock(&g_plan_mutex);

    if (!done) return false;

    g_confirm_action = action;
    if (rc == 0 && parse_plan(out)) {
        g_status_message.clear();
        g_screen = Screen::Confirm;
    } else {
        g_status_message = one_line(backend_error_message(out), 44);
    }
    return true;
}

void cancel_confirm()
{
    g_screen = Screen::Detail;
    g_confirm_action.clear();
    g_confirm_lines.clear();
    g_confirm_focus = 0;
}

void start_confirm(const std::string &action)
{
    StoreApp *app = selected_app();
    if (!app) return;
    if (g_job_running) {
        g_status_message = "Operation already running";
        return;
    }
    if (g_plan_running) {
        g_status_message = "Plan check already running";
        return;
    }
    g_confirm_action = action;
    g_confirm_focus = 0;
    if (action == "uninstall") {
        g_confirm_lines = {"Delete " + app->name, "Remove installed Debian package.", "Disk free: " + g_free_space};
        g_screen = Screen::Confirm;
        return;
    }
    start_plan_check(action, app->id);
}

void start_detail_install_action(StoreApp *app)
{
    if (!app) {
        g_status_message = "No selected app";
        return;
    }
    if (app->installed) {
        g_status_message = "Already installed";
        return;
    }
    if (!can_install_app(*app)) {
        g_status_message = "Only approved apps can install";
        return;
    }
    start_confirm("install");
}

void start_detail_reinstall_action(StoreApp *app)
{
    if (!app) {
        g_status_message = "No selected app";
        return;
    }
    if (!app->installed) {
        g_status_message = "App is not installed";
        return;
    }
    if (!can_reinstall_app(*app)) {
        g_status_message = "Only approved apps can install";
        return;
    }
    start_confirm("reinstall");
}

void start_detail_upgrade_action(StoreApp *app)
{
    if (!app) {
        g_status_message = "No selected app";
        return;
    }
    if (!app->installed) {
        g_status_message = "App is not installed";
        return;
    }
    if (!can_install_app(*app)) {
        g_status_message = "Only approved apps can install";
        return;
    }
    if (!can_upgrade_app(*app)) {
        g_status_message = "Already latest";
        return;
    }
    start_confirm("upgrade");
}

void start_detail_delete_action(StoreApp *app)
{
    if (!app) {
        g_status_message = "No selected app";
    } else if (!app->installed) {
        g_status_message = "App is not installed";
    } else {
        start_confirm("uninstall");
    }
}

void cycle_detail_screenshot(int delta)
{
    StoreApp *app = ensure_selected_app();
    if (!app) {
        g_status_message = "No selected app";
        return;
    }
    std::vector<std::string> screenshots = detail_screenshot_paths(g_app_dir, *app);
    normalize_detail_image_state(*app, screenshots);
    int count = static_cast<int>(screenshots.size());
    if (count == 0) {
        g_status_message = "No screenshots for this app";
        return;
    }
    g_detail_image_index = (g_detail_image_index + delta + count) % count;
    g_status_message.clear();
    show_screenshots_overlay();
}

void scroll_detail_description(int delta)
{
    StoreApp *app = ensure_selected_app();
    if (!app) {
        g_status_message = "No selected app";
        return;
    }
    std::vector<std::string> lines = detail_description_lines(*app);
    if (lines.empty()) lines.push_back("-");
    normalize_detail_description_state(*app, lines);
    int max_scroll = std::max(0, static_cast<int>(lines.size()) - 2);
    if (max_scroll == 0) return;
    int next = g_detail_description_scroll + delta;
    g_detail_description_scroll = std::max(0, std::min(max_scroll, next));
    g_status_message.clear();
}

void start_backend_job(const std::string &action, StoreApp *app, const std::string &sudo_password)
{
    if (!app) return;
    if (g_job_running) {
        g_status_message = "Operation already running";
        return;
    }

    g_job_running = true;
    g_job_pending_start = true;
    g_job_action = action;
    g_job_app_id = app->id;
    g_job_title = app->name;
    g_job_stage.clear();
    g_job_detail = "Preparing package worker";
    g_job_output.clear();
    g_job_sudo_password = sudo_password;
    g_job_progress = -1;
    g_job_rc = -1;
    g_job_done = false;
    g_job_start_tick = lv_tick_get();
    g_status_message = "Preparing " + one_line(app->name, 18) + "...";
    g_screen = Screen::Progress;
    app_tracef("job start action=%s app=%s title=%s installed_before=%d version=%s",
               g_job_action.c_str(), g_job_app_id.c_str(), g_job_title.c_str(),
               app->installed ? 1 : 0,
               app->installed_version.empty() ? "-" : app->installed_version.c_str());
    render();
    lv_refr_now(nullptr);
}

void execute_confirm()
{
    StoreApp *app = selected_app();
    if (!app || g_confirm_action.empty()) return;
    g_sudo_action = g_confirm_action;
    g_sudo_app_id = app->id;
    g_sudo_app_title = app->name;
    g_sudo_password_input.clear();
    g_sudo_message = "Enter sudo password for package operation.";
    g_screen = Screen::SudoPassword;
}

void execute_sudo_password()
{
    StoreApp *app = selected_app();
    if (!app || app->id != g_sudo_app_id) {
        app = nullptr;
        for (StoreApp &candidate : g_apps) {
            if (candidate.id == g_sudo_app_id) {
                app = &candidate;
                break;
            }
        }
    }
    if (!app || g_sudo_action.empty()) {
        g_sudo_message = "Selected app is no longer available.";
        return;
    }
    if (g_sudo_password_input.empty()) {
        g_sudo_message = "Password is required for sudo.";
        return;
    }

    std::string action = g_sudo_action;
    std::string password = g_sudo_password_input;
    g_confirm_action.clear();
    g_confirm_lines.clear();
    g_sudo_action.clear();
    g_sudo_app_id.clear();
    g_sudo_app_title.clear();
    g_sudo_password_input.clear();
    g_sudo_message = "Enter sudo password for package operation.";
    start_backend_job(action, app, password);
}

void handle_key(const KeyEvent &key)
{
    uint32_t key_start = lv_tick_get();
    Screen before_screen = g_screen;
    app_tracef("handle_key begin screen=%s code=%u ch=%d release=%d repeat=%d",
               screen_name(before_screen), key.code, static_cast<int>(key.ch),
               key.release ? 1 : 0, key.repeated ? 1 : 0);
    if (key.code == KEY_ESC) {
        if (key.release) {
            bool do_back = g_esc_pressed && !g_esc_long_consumed &&
                           lv_tick_elaps(g_esc_press_tick) < kEscLongPressMs;
            g_esc_pressed = false;
            g_esc_long_consumed = false;
            if (do_back) {
                navigate_back();
                render();
            }
        } else if (!key.repeated) {
            g_esc_pressed = true;
            g_esc_long_consumed = false;
            g_esc_press_tick = lv_tick_get();
        } else if (g_esc_pressed && !g_esc_long_consumed &&
                   lv_tick_elaps(g_esc_press_tick) >= kEscLongPressMs) {
            g_esc_long_consumed = true;
            request_quit();
        }
        app_tracef("handle_key end elapsed=%ums before=%s after=%s code=%u esc=1",
                   lv_tick_elaps(key_start), screen_name(before_screen),
                   screen_name(g_screen), key.code);
        return;
    }
    if (key.release) {
        app_tracef("handle_key end elapsed=%ums before=%s after=%s code=%u release=1",
                   lv_tick_elaps(key_start), screen_name(before_screen),
                   screen_name(g_screen), key.code);
        return;
    }
    switch (g_screen) {
        case Screen::StartupSync:
            if (g_startup_sync_failed && key.code == KEY_ENTER) {
                request_quit();
            } else if (!g_startup_sync_failed && (key_matches(key, 's', KEY_S) || key_matches(key, '4', KEY_4))) {
                cancel_startup_sync_and_open_registry();
            } else if (key_matches(key, 'q', KEY_Q)) {
                request_quit();
            }
            break;
        case Screen::Home:
            if ((key.code == KEY_UP || key.code == KEY_F || key.ch == 'f') && !g_visible.empty()) {
                g_selected = g_selected == 0 ? static_cast<int>(g_visible.size()) - 1 : g_selected - 1;
            } else if ((key.code == KEY_DOWN || key.code == KEY_X || key.ch == 'x') && !g_visible.empty()) {
                g_selected = (g_selected + 1) % static_cast<int>(g_visible.size());
            } else if ((key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<') && !g_categories.empty()) {
                g_category = g_category == 0 ? static_cast<int>(g_categories.size()) - 1 : g_category - 1;
                rebuild_visible();
            } else if ((key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>') && !g_categories.empty()) {
                g_category = (g_category + 1) % static_cast<int>(g_categories.size());
                rebuild_visible();
            } else if (key_matches(key, '4', KEY_4)) {
                open_registry_screen();
            } else if (key_matches(key, '5', KEY_5)) {
                open_share_code_screen();
            } else if (key_matches(key, '6', KEY_6)) {
                open_search_screen();
            } else if (key_matches(key, '7', KEY_7)) {
                StoreApp *app = selected_app();
                if (app && can_install_app(*app)) {
                    start_confirm(app->installed ? "reinstall" : "install");
                } else if (app) {
                    g_status_message = "Only approved apps can install";
                } else {
                    g_status_message = "No selected app";
                }
            } else if (key_matches(key, '8', KEY_8)) {
                open_detail_screen();
            } else if (key.code == KEY_TAB) {
                cycle_sort_rule();
            } else if (key.code == KEY_ENTER) {
                open_detail_screen();
            } else if (key_matches(key, 's', KEY_S)) {
                open_registry_screen();
            } else if (key_matches(key, 'q', KEY_Q)) {
                request_quit();
            }
            break;
        case Screen::Detail: {
            StoreApp *app = ensure_selected_app();
            if (key.code == KEY_UP || key.code == KEY_F || key.ch == 'f') {
                scroll_detail_description(-1);
            } else if (key.code == KEY_DOWN || key.code == KEY_X || key.ch == 'x') {
                scroll_detail_description(1);
            } else if (key_matches(key, '4', KEY_4) || key_matches(key, 'b', KEY_B)) {
                g_screen = Screen::Home;
            } else if (key_matches(key, '5', KEY_5)) {
                open_screenshots_screen();
            } else if (key_matches(key, '6', KEY_6)) {
                if (app && app->installed) start_detail_reinstall_action(app);
                else start_detail_install_action(app);
            } else if (key_matches(key, '7', KEY_7)) {
                start_detail_upgrade_action(app);
            } else if (key_matches(key, '8', KEY_8)) {
                start_detail_delete_action(app);
            } else if (app && key_matches(key, 'i', KEY_I)) {
                if (can_install_app(*app)) {
                    start_confirm(app->installed ? "reinstall" : "install");
                } else {
                    g_status_message = "Only approved apps can install";
                }
            } else if (app && app->installed && can_install_app(*app) && key_matches(key, 'u', KEY_U)) {
                start_confirm("upgrade");
            } else if (app && app->installed && key_matches(key, 'd', KEY_D)) {
                start_confirm("uninstall");
            }
            break;
        }
        case Screen::Screenshots:
            if (key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<') {
                cycle_detail_screenshot(-1);
            } else if (key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>') {
                cycle_detail_screenshot(1);
            } else if (key_matches(key, '4', KEY_4) || key_matches(key, 'b', KEY_B)) {
                g_screen = Screen::Detail;
            }
            break;
        case Screen::Confirm:
            if (key_matches(key, 'b', KEY_B) || key_matches(key, 'n', KEY_N)) {
                cancel_confirm();
            } else if (key_matches(key, 'y', KEY_Y)) {
                execute_confirm();
            } else if (key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<') {
                g_confirm_focus = 0;
            } else if (key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>') {
                g_confirm_focus = 1;
            } else if (key.code == KEY_TAB) {
                g_confirm_focus = 1 - g_confirm_focus;
            } else if (key.code == KEY_ENTER) {
                if (g_confirm_focus == 0) execute_confirm();
                else cancel_confirm();
            }
            break;
        case Screen::SudoPassword:
            if (key.code == KEY_BACKSPACE && !g_sudo_password_input.empty()) {
                g_sudo_password_input.pop_back();
                g_sudo_message = "Enter sudo password for package operation.";
            } else if (key.code == KEY_ENTER) {
                execute_sudo_password();
            } else if (key.ch >= 32 && key.ch <= 126 && g_sudo_password_input.size() < 64) {
                g_sudo_password_input.push_back(key.ch);
                g_sudo_message = "Enter runs package operation with sudo.";
            }
            break;
        case Screen::Progress:
            if (!g_job_running && !g_job_pending_start && key_matches(key, 'b', KEY_B)) {
                g_screen = Screen::Detail;
            }
            break;
        case Screen::ErrorDialog:
            if (key.code == KEY_ENTER || key_matches(key, 'y', KEY_Y) ||
                key_matches(key, 'b', KEY_B)) {
                navigate_back();
            }
            break;
        case Screen::Registry:
            if (key_matches(key, 'b', KEY_B)) {
                g_screen = Screen::Home;
            } else if (key.code == KEY_UP || key.code == KEY_DOWN) {
                g_registry_page_focus = 1 - g_registry_page_focus;
            } else if (key_matches(key, 'a', KEY_A)) {
                open_registry_add_screen();
            } else if (key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<') {
                if (g_registry_page_focus == 0) {
                    select_region(adjacent_region(-1));
                } else if (!g_registry_entries.empty()) {
                    g_registry_selected = g_registry_selected == 0 ?
                        static_cast<int>(g_registry_entries.size()) - 1 : g_registry_selected - 1;
                }
            } else if (key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>') {
                if (g_registry_page_focus == 0) {
                    select_region(adjacent_region(1));
                } else if (!g_registry_entries.empty()) {
                    g_registry_selected = (g_registry_selected + 1) % static_cast<int>(g_registry_entries.size());
                }
            } else if (key_matches(key, 'r', KEY_R)) {
                sync_catalog(true);
            } else if (key_matches(key, 't', KEY_T)) {
                toggle_selected_registry();
            } else if (key_matches(key, 'd', KEY_D)) {
                delete_selected_registry();
            } else if (key_matches(key, 'e', KEY_E)) {
                edit_selected_registry();
            } else if (key.code == KEY_ENTER && g_registry_page_focus == 1) {
                edit_selected_registry();
            }
            break;
        case Screen::RegistryEdit: {
            std::string &field = g_registry_focus == 0 ? g_registry_name_input : g_registry_input;
            if (key.code == KEY_BACKSPACE && !field.empty()) {
                field.pop_back();
            } else if (key.code == KEY_ENTER) {
                add_registry_from_input();
            } else if (key.ch >= 32 && key.ch <= 126 &&
                       (g_registry_focus == 0 ? g_registry_name_input.size() < 48 : g_registry_input.size() < 180)) {
                field.push_back(key.ch);
            } else if (key.code == KEY_UP || key.code == KEY_DOWN) {
                g_registry_focus = 1 - g_registry_focus;
            } else if (key.code == KEY_LEFT && !g_registry_edit_url.empty() && !g_registry_entries.empty()) {
                g_registry_selected = g_registry_selected == 0 ?
                    static_cast<int>(g_registry_entries.size()) - 1 : g_registry_selected - 1;
                edit_selected_registry();
            } else if (key.code == KEY_RIGHT && !g_registry_edit_url.empty() && !g_registry_entries.empty()) {
                g_registry_selected = (g_registry_selected + 1) % static_cast<int>(g_registry_entries.size());
                edit_selected_registry();
            }
            break;
        }
        case Screen::ShareCode:
            if (key.code == KEY_BACKSPACE && !g_share_code_input.empty()) {
                g_share_code_input.pop_back();
            } else if (key.code == KEY_ENTER) {
                open_share_code_match();
            } else if (key.ch >= 32 && key.ch <= 126 && g_share_code_input.size() < 64) {
                if (g_share_code_input.empty() && key.ch == 'c' &&
                    lv_tick_elaps(g_share_code_open_tick) < 250) {
                    break;
                }
                g_share_code_input.push_back(key.ch);
                g_share_code_message = "Enter opens the app detail page.";
            }
            break;
        case Screen::Search:
            if (g_search_results_active && !g_search_results.empty() &&
                (key.code == KEY_UP || key.code == KEY_F || key.ch == 'f')) {
                g_search_selected = g_search_selected == 0 ?
                    static_cast<int>(g_search_results.size()) - 1 : g_search_selected - 1;
            } else if (g_search_results_active && !g_search_results.empty() &&
                       (key.code == KEY_DOWN || key.code == KEY_X || key.ch == 'x')) {
                g_search_selected = (g_search_selected + 1) % static_cast<int>(g_search_results.size());
            } else if (key.code == KEY_BACKSPACE && !g_search_input.empty()) {
                g_search_input.pop_back();
                clear_search_results();
                g_search_message = "Enter searches matching apps.";
            } else if (key.code == KEY_ENTER) {
                open_selected_search_result();
            } else if (key.ch >= 32 && key.ch <= 126 && g_search_input.size() < 64) {
                g_search_input.push_back(key.ch);
                clear_search_results();
                g_search_message = "Enter searches matching apps.";
            }
            break;
    }
    render();
    app_tracef("handle_key end elapsed=%ums before=%s after=%s code=%u ch=%d",
               lv_tick_elaps(key_start), screen_name(before_screen), screen_name(g_screen),
               key.code, static_cast<int>(key.ch));
}

char lower_printable(const char *utf8)
{
    if (!utf8 || !utf8[0] || utf8[1]) return 0;
    unsigned char ch = static_cast<unsigned char>(utf8[0]);
    if (!std::isprint(ch)) return 0;
    return static_cast<char>(std::tolower(ch));
}

void handle_keyboard_event(lv_event_t *event)
{
    auto *item = static_cast<key_item *>(lv_event_get_param(event));
    if (!item) return;
    KeyEvent key;
    key.code = item->key_code;
    key.mods = item->mods;
    key.ch = lower_printable(item->utf8);
    key.release = item->key_state == KBD_KEY_RELEASED;
    key.repeated = item->key_state == KBD_KEY_REPEATED;
    handle_key(key);
}

void build_ui()
{
    g_root = lv_screen_active();
    lv_obj_set_size(g_root, kScreenWidth, kScreenHeight);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_root, handle_keyboard_event, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), nullptr);

    g_group = lv_group_create();
    lv_group_add_obj(g_group, g_root);
    lv_group_focus_obj(g_root);
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (indev) lv_indev_set_group(indev, g_group);
}

}  // namespace

void ui_init(int, char **argv)
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    cp0_zmq_log_init();

    g_app_dir = dirname_of(argv && argv[0] ? argv[0] : nullptr);
    set_backend_script_path(resolve_script_path(g_app_dir));
    std::fprintf(stderr, "[AppStore UI] ui_init argv0=%s app_dir=%s backend=%s\n",
                 argv && argv[0] ? argv[0] : "-", g_app_dir.c_str(), backend_script_path().c_str());
    app_tracef("ui_init argv0=%s app_dir=%s backend=%s",
               argv && argv[0] ? argv[0] : "-", g_app_dir.c_str(), backend_script_path().c_str());

    init_runtime_fonts(g_app_dir);
    build_ui();
    update_top_status_cache(true);
    g_screen = Screen::StartupSync;
    g_startup_sync_active = true;
    g_sync_status.detail = "Preparing catalog sync...";
    g_sync_status.phase = "startup";
    g_sync_status.url = g_region_registry_url;
    render();
    sync_shared_registry_config_on_startup();
    g_sync_status.url = g_region_registry_url;
    render();
    g_sync_timer = lv_timer_create(sync_timer_cb, kSyncAnimRefreshMs, nullptr);
    g_refresh_timer = lv_timer_create(refresh_timer_cb, 250, nullptr);
    g_esc_hold_timer = lv_timer_create(esc_hold_timer_cb, 50, nullptr);
    g_job_timer = lv_timer_create(job_timer_cb, kJobPollIntervalMs, nullptr);
    request_summary_refresh();
    sync_catalog();
    render();
}

void ui_loop()
{
}

void ui_deinit()
{
    if (g_esc_hold_timer) lv_timer_delete(g_esc_hold_timer);
    if (g_refresh_timer) lv_timer_delete(g_refresh_timer);
    if (g_job_timer) lv_timer_delete(g_job_timer);
    if (g_sync_timer) lv_timer_delete(g_sync_timer);
    stop_backend_service();
}

bool ui_should_quit()
{
    return g_quit_requested != 0;
}
