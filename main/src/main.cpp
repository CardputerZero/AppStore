#include "keyboard_input.h"
#include "compat/input_keys.h"

#include "lvgl/lvgl.h"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#if LV_USE_SDL
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#endif

#if LV_USE_EVDEV
#include <pthread.h>
#endif

namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kHeaderHeight = 24;
constexpr int kLineHeight = 15;
constexpr uint32_t kEscLongPressMs = 1200;
constexpr uint32_t kJobStartDelayMs = 80;
constexpr uint32_t kJobPollIntervalMs = 250;

struct StoreApp {
    std::string id;
    std::string share_code;
    std::string name;
    std::string version;
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
};

struct RegistryEntry {
    std::string url;
    std::string name;
    std::string status;
    std::string count;
    std::string updated_at;
    std::string error;
    bool enabled = true;
};

enum class Screen {
    Home,
    Detail,
    Confirm,
    Progress,
    Loading,
    Registry,
    RegistryEdit,
    ShareCode,
};

struct KeyEvent {
    uint32_t code = 0;
    uint32_t mods = 0;
    char ch = 0;
    bool release = false;
    bool repeated = false;
};

enum class SortRule {
    Default,
    New,
    Old,
    AtoZ,
    ZtoA,
};

lv_obj_t *g_root = nullptr;
lv_indev_t *g_keyboard_indev = nullptr;
lv_group_t *g_group = nullptr;
lv_timer_t *g_refresh_timer = nullptr;
lv_timer_t *g_esc_hold_timer = nullptr;
lv_timer_t *g_job_timer = nullptr;
lv_timer_t *g_sync_timer = nullptr;
volatile sig_atomic_t g_quit_requested = 0;

std::string g_app_dir = ".";
std::string g_script_path = "appstore.py";
std::string g_status_message;
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
Screen g_screen = Screen::Home;
bool g_default_category_applied = false;
SortRule g_sort_rule = SortRule::Default;
std::string g_confirm_action;
std::vector<std::string> g_confirm_lines;
std::string g_loading_title;
std::string g_loading_detail;
std::string g_registry_input = "https://cardputerzero.github.io/generated/registry-index.json";
std::string g_registry_edit_url;
std::string g_registry_name_input;
int g_registry_focus = 0;
std::string g_share_code_input;
std::string g_share_code_message = "Enter a code from CardputerZero Hub.";
bool g_job_running = false;
bool g_job_pending_start = false;
std::string g_job_action;
std::string g_job_app_id;
std::string g_job_title;
std::string g_job_output_path;
std::string g_job_rc_path;
std::string g_job_stage;
std::string g_job_detail;
int g_job_progress = -1;
pid_t g_job_pid = -1;
uint32_t g_job_start_tick = 0;
uint32_t g_share_code_open_tick = 0;
uint32_t g_esc_press_tick = 0;
bool g_esc_pressed = false;
bool g_esc_long_consumed = false;
pthread_mutex_t g_sync_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_sync_running = false;
bool g_sync_done = false;
bool g_sync_refresh_registries = false;
std::string g_sync_output;

const char *getenv_default(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value ? value : fallback;
}

void request_quit()
{
    g_quit_requested = 1;
}

void handle_signal(int)
{
    request_quit();
}

std::string dirname_of(const char *argv0)
{
    if (!argv0 || !argv0[0]) return ".";
    char resolved[1024] = {};
    const char *path = argv0;
    if (realpath(argv0, resolved)) path = resolved;
    std::string value(path);
    size_t slash = value.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return value.substr(0, slash);
}

std::string parent_dir(const std::string &path)
{
    if (path.empty()) return ".";
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string shell_quote(const std::string &value)
{
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') out += "'\\''";
        else out += ch;
    }
    out += "'";
    return out;
}

std::string run_capture(const std::string &cmd)
{
    std::string output;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return output;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;
    pclose(pipe);
    return output;
}

std::string read_text_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string tsv_unescape(const std::string &value)
{
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char next = value[++i];
            if (next == 't') out += '\t';
            else if (next == 'n') out += '\n';
            else if (next == 'r') out += '\r';
            else out += next;
        } else {
            out += value[i];
        }
    }
    return out;
}

std::vector<std::string> split_tab(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : line) {
        if (ch == '\t') {
            out.push_back(tsv_unescape(cur));
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out.push_back(tsv_unescape(cur));
    return out;
}

std::string one_line(std::string value, size_t max_len)
{
    for (char &ch : value) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    if (value.size() > max_len) {
        value.resize(max_len > 3 ? max_len - 3 : max_len);
        value += "...";
    }
    return value;
}

std::string trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

bool has_blocking_missing(const std::string &missing)
{
    std::istringstream stream(missing);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty() && token != "root-write") return true;
    }
    return false;
}

std::string missing_install_message(const std::string &missing)
{
    if (missing.find("deb-only") != std::string::npos) return "Only .deb packages are supported";
    if (missing.find("md5") != std::string::npos) return "Registry MD5 is required";
    if (missing.find("package-name") != std::string::npos) return "Deb package name is required";
    if (missing.find("package") != std::string::npos) return "Download URL is required";
    return "Install metadata is incomplete";
}

bool key_matches(const KeyEvent &key, char ch, uint32_t code)
{
    return key.ch == ch || key.code == code;
}

std::string job_action_label(const std::string &action)
{
    if (action == "uninstall") return "Uninstalling";
    if (action == "reinstall") return "Reinstalling";
    return "Installing";
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

std::string backend_error_message(const std::string &out)
{
    std::string fallback;
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.empty() || fields[0] == "PROGRESS") continue;
        if (fields[0] == "ERROR") {
            if (fields.size() >= 2 && !fields[1].empty()) return fields[1];
            return "Operation failed";
        }
        if (!trim(line).empty()) fallback = line;
    }
    if (!fallback.empty()) return fallback;
    return out.empty() ? "Operation failed" : out;
}

std::string sync_status_message(const std::string &out)
{
    std::string error;
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.empty()) continue;
        if (fields[0] == "ERROR" && fields.size() >= 2 && !fields[1].empty()) {
            error = fields[1];
        } else if (fields[0] == "SYNC" && fields.size() >= 6) {
            if (fields[5] == "Catalog synced") return "";
            if (!fields[5].empty()) return fields[5];
        }
    }
    return error;
}

std::string upper_ascii(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string match_key(std::string value)
{
    std::string out;
    for (char ch : value) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isspace(uch)) out += static_cast<char>(std::tolower(uch));
    }
    return out;
}

std::string first_csv(std::string value)
{
    size_t comma = value.find(',');
    if (comma != std::string::npos) value.resize(comma);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string app_sort_name(const StoreApp &app)
{
    return match_key(app.name);
}

std::string sort_rule_label(SortRule rule)
{
    switch (rule) {
        case SortRule::New: return "new";
        case SortRule::Old: return "old";
        case SortRule::AtoZ: return "a-z";
        case SortRule::ZtoA: return "z-a";
        case SortRule::Default:
        default: return "def";
    }
}

bool app_title_less(const StoreApp &a, const StoreApp &b)
{
    std::string an = app_sort_name(a);
    std::string bn = app_sort_name(b);
    if (an != bn) return an < bn;
    return a.id < b.id;
}

void sort_apps(std::vector<StoreApp> &apps)
{
    std::stable_sort(apps.begin(), apps.end(), [](const StoreApp &a, const StoreApp &b) {
        switch (g_sort_rule) {
            case SortRule::AtoZ:
                return app_title_less(a, b);
            case SortRule::ZtoA:
                return app_title_less(b, a);
            case SortRule::New:
                if (a.updated_at != b.updated_at) {
                    if (a.updated_at.empty()) return false;
                    if (b.updated_at.empty()) return true;
                    return a.updated_at > b.updated_at;
                }
                return app_title_less(a, b);
            case SortRule::Old:
                if (a.updated_at != b.updated_at) {
                    if (a.updated_at.empty()) return false;
                    if (b.updated_at.empty()) return true;
                    return a.updated_at < b.updated_at;
                }
                return app_title_less(a, b);
            case SortRule::Default:
            default:
                if (a.recommended != b.recommended) return a.recommended && !b.recommended;
                return app_title_less(a, b);
        }
    });
}

bool file_exists(const std::string &path)
{
    if (path.empty()) return false;
    std::ifstream file(path);
    return file.good();
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
}

std::string icon_file_path(const StoreApp &app)
{
    std::string image = first_csv(app.images);
    if (image.empty()) return "";
    if (image.rfind("file://", 0) == 0) image = image.substr(7);
    if (!image.empty() && image[0] == '/') return file_exists(image) ? image : "";
    std::string root = parent_dir(g_app_dir);
    std::string candidate = root + "/" + image;
    return file_exists(candidate) ? candidate : "";
}

std::string packaged_image_path(const std::string &name)
{
    std::vector<std::string> candidates = {
        parent_dir(g_app_dir) + "/share/images/" + name,
        g_app_dir + "/share/images/" + name,
        "/usr/share/APPLaunch/share/images/" + name,
    };
    for (const auto &candidate : candidates) {
        if (file_exists(candidate)) return candidate;
    }
    return "";
}

std::string lvgl_posix_src(const std::string &path)
{
    if (path.empty()) return "";
    return "A:" + path;
}

std::string backend_cmd(const std::string &args)
{
    return "python3 " + shell_quote(g_script_path) + " " + args;
}

std::string resolve_script_path(const std::string &binary_dir)
{
    const char *script_env = std::getenv("M5APPSTORE_SCRIPT");
    if (script_env && script_env[0]) return script_env;

    std::vector<std::string> candidates = {
        binary_dir + "/appstore.py",
        parent_dir(binary_dir) + "/appstore.py",
        parent_dir(binary_dir) + "/share/appstore/appstore.py",
        "/usr/share/APPLaunch/bin/appstore.py",
        "/usr/share/APPLaunch/share/appstore/appstore.py",
    };
    for (const auto &candidate : candidates) {
        if (file_exists(candidate)) return candidate;
    }
    return binary_dir + "/appstore.py";
}

void rebuild_visible()
{
    g_visible.clear();
    std::string cat = current_category_name();
    for (int i = 0; i < static_cast<int>(g_apps.size()); ++i) {
        bool show = cat == "All" ||
            (cat == "Recommended" && g_apps[i].recommended) ||
            g_apps[i].category == cat;
        if (show) g_visible.push_back(i);
    }
    if (g_selected >= static_cast<int>(g_visible.size())) g_selected = static_cast<int>(g_visible.size()) - 1;
    if (g_selected < 0) g_selected = 0;
}

StoreApp *selected_app()
{
    if (g_visible.empty() || g_selected < 0 || g_selected >= static_cast<int>(g_visible.size())) return nullptr;
    return &g_apps[g_visible[g_selected]];
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

void refresh_summary()
{
    StoreApp *previous_app = selected_app();
    std::string previous_app_id = previous_app ? previous_app->id : "";
    std::string previous_category = current_category_name();
    std::string output = run_capture(backend_cmd("--summary"));
    std::istringstream stream(output);
    std::string line;
    std::vector<StoreApp> apps;
    std::vector<std::string> cats;
    std::string warning;
    bool saw_meta = false;

    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.empty()) continue;
        if (fields[0] == "META" && fields.size() >= 5) {
            saw_meta = true;
            g_repo_status = fields[2];
            g_free_space = fields[3];
            g_root_path = fields[4];
        } else if (fields[0] == "CAT" && fields.size() >= 2) {
            cats.push_back(fields[1]);
        } else if (fields[0] == "WARN" && fields.size() >= 2) {
            warning = fields[1];
        } else if (fields[0] == "APP" && fields.size() >= 13) {
            StoreApp app;
            app.id = fields[1];
            app.name = fields[2];
            app.version = fields[3];
            app.category = fields[4];
            app.installed = fields[5] == "1";
            app.recommended = fields[6] == "1";
            app.size = fields[7];
            app.description = fields[8];
            app.author = fields[9];
            app.git_url = fields[10];
            app.images = fields[11];
            app.dependencies = fields[12];
            if (fields.size() >= 14) app.share_code = fields[13];
            if (fields.size() >= 15) app.registry_name = fields[14];
            if (fields.size() >= 16) app.updated_at = fields[15];
            apps.push_back(app);
        }
    }

    if (!apps.empty()) sort_apps(apps);
    if (!cats.empty()) g_categories = cats;
    if (saw_meta) g_apps = apps;
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
    if (!warning.empty()) g_status_message = one_line(warning, 54);
}

void refresh_registries()
{
    g_registry_lines.clear();
    g_registry_entries.clear();
    std::string output = run_capture(backend_cmd("--registries"));
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.size() >= 5 && fields[0] == "REG") {
            RegistryEntry entry;
            entry.url = fields[1];
            entry.status = fields[2];
            entry.count = fields[3];
            entry.updated_at = fields[4];
            if (fields.size() >= 6) entry.error = fields[5];
            if (fields.size() >= 7) entry.enabled = fields[6] != "0";
            entry.name = fields.size() >= 8 && !fields[7].empty() ? fields[7] : entry.url;
            std::string item = std::string(entry.enabled ? "on  " : "off ") + entry.status + "  " + entry.count + " apps";
            if (!entry.updated_at.empty()) item += "  " + entry.updated_at.substr(5, 5);
            if (!entry.error.empty()) {
                item += "  " + one_line(entry.error, 30);
            } else {
                item += "  " + entry.url;
            }
            g_registry_entries.push_back(entry);
            g_registry_lines.push_back(item);
        }
    }
    if (g_registry_lines.empty()) {
        g_registry_lines.push_back("not synced  0 apps  " + g_registry_input);
    }
    if (g_registry_selected >= static_cast<int>(g_registry_entries.size())) {
        g_registry_selected = std::max(0, static_cast<int>(g_registry_entries.size()) - 1);
    }
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

std::string current_time_text()
{
    char buf[8] = "--:--";
    std::time_t now = std::time(nullptr);
    std::tm tm_value {};
#if defined(_WIN32)
    localtime_s(&tm_value, &now);
#else
    localtime_r(&now, &tm_value);
#endif
    std::strftime(buf, sizeof(buf), "%H:%M", &tm_value);
    return buf;
}

void draw_system_bar()
{
    if (!draw_packaged_image("store_wordmark.png", 6, 5)) {
        strong_label(g_root, "STORE", 6, 5, 78, 22, &lv_font_montserrat_20, 0xFFFFFF);
    }

    box(202, 5, 43, 15, 0x333333, 0x333333, 0);
    center_strong_label(g_root, current_time_text(), 204, 6, 38, 13,
                        &lv_font_montserrat_14, 0xD7D7D7);

    box(247, 5, 31, 15, 0x333333, 0x333333, 0);
    bool sync_active = false;
    pthread_mutex_lock(&g_sync_mutex);
    sync_active = g_sync_running;
    pthread_mutex_unlock(&g_sync_mutex);
    if (sync_active) {
        int phase = static_cast<int>((lv_tick_get() / 180) % 4);
        uint32_t dim = 0x2B6B7A;
        box(260, 7, 5, 3, phase == 0 ? 0x00D8FF : dim, phase == 0 ? 0x00D8FF : dim, 0);
        box(268, 10, 3, 5, phase == 1 ? 0x00D8FF : dim, phase == 1 ? 0x00D8FF : dim, 0);
        box(260, 17, 5, 3, phase == 2 ? 0x00D8FF : dim, phase == 2 ? 0x00D8FF : dim, 0);
        box(252, 10, 3, 5, phase == 3 ? 0x00D8FF : dim, phase == 3 ? 0x00D8FF : dim, 0);
    } else {
        box(253, 16, 5, 3, 0x00BFFF, 0x00BFFF, 0);
        box(261, 13, 5, 6, 0x00BFFF, 0x00BFFF, 0);
        box(269, 10, 5, 9, 0x00BFFF, 0x00BFFF, 0);
    }

    box(281, 5, 37, 15, 0x333333, 0x333333, 0);
    center_strong_label(g_root, "100%", 282, 6, 35, 13, &lv_font_montserrat_14, 0xD7D7D7);
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
    std::string path = icon_file_path(app);
    if (path.empty()) return false;
    g_render_image_sources.push_back(lvgl_posix_src(path));

    constexpr int clip_x = 19;
    constexpr int clip_y = 40;
    constexpr int clip_w = 82;
    constexpr int clip_h = 82;
    lv_obj_t *clip = lv_obj_create(g_root);
    lv_obj_remove_style_all(clip);
    lv_obj_set_pos(clip, clip_x, clip_y);
    lv_obj_set_size(clip, clip_w, clip_h);
    lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clip, 0, 0);
    lv_obj_set_style_radius(clip, 14, 0);
    lv_obj_set_style_clip_corner(clip, true, 0);
    lv_obj_set_style_pad_all(clip, 0, 0);

    lv_obj_t *icon = lv_image_create(clip);
    lv_image_set_scale(icon, 210);
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
    std::string path = packaged_image_path(name);
    if (path.empty()) return false;
    g_render_image_sources.push_back(lvgl_posix_src(path));
    lv_obj_t *image = lv_image_create(g_root);
    lv_image_set_src(image, g_render_image_sources.back().c_str());
    lv_obj_set_pos(image, x, y);
    return true;
#else
    (void)name;
    (void)x;
    (void)y;
    return false;
#endif
}

void draw_home_icon_panel(const StoreApp *app)
{
    if (!draw_packaged_image("store_arrow_up.png", 53, 27)) {
        center_strong_label(g_root, "^", 61, 28, 28, 20, &lv_font_montserrat_20, 0xFF6A3D);
    }
    box(19, 40, 82, 82, 0x202020, 0x4D4D4D, 2, 14, LV_OPA_COVER);
    if (!app) {
        center_strong_label(g_root, "-", 49, 82, 24, 26, &lv_font_montserrat_20, 0x9A9A9A);
        if (!draw_packaged_image("store_arrow_down.png", 53, 127)) {
            center_strong_label(g_root, "v", 61, 135, 28, 20, &lv_font_montserrat_20, 0xFF6A3D);
        }
        return;
    }

    if (!draw_app_icon_image(*app)) {
        center_strong_label(g_root, app_initial(*app), 19, 66, 82, 30,
                            &lv_font_montserrat_20, app->installed ? 0x52D05D : 0xFFFFFF);
    }
    if (!draw_packaged_image("store_arrow_down.png", 53, 127)) {
        center_strong_label(g_root, "v", 61, 135, 28, 20, &lv_font_montserrat_20, 0xFF6A3D);
    }
}

void draw_nav_bar()
{
    box(1, 148, 117, 20, 0x333380, 0x333380, 0);
    if (!draw_packaged_image("store_arrow_left.png", 5, 151)) {
        strong_label(g_root, "<", 8, 149, 17, 19, &lv_font_montserrat_20, 0xFF6A3D);
    }
    std::string cat = current_category_name();
    center_strong_label(g_root, one_line(upper_ascii(cat), 11), 29, 151, 70, 16,
                        &lv_font_montserrat_14, 0xFFFFFF, LV_LABEL_LONG_DOT);
    if (!draw_packaged_image("store_arrow_right.png", 107, 151)) {
        strong_label(g_root, ">", 111, 149, 17, 19, &lv_font_montserrat_20, 0xFF6A3D);
    }
}

void header(const std::string &title, const std::string &right)
{
    lv_obj_t *bar = lv_obj_create(g_root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, kScreenWidth, kHeaderHeight);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x111923), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    label(bar, title, 8, 5, 160, 15, &lv_font_montserrat_14, 0xF4F8FB);
    label(bar, right, 170, 6, 142, 13, &lv_font_montserrat_10, 0x8BE28B, LV_LABEL_LONG_DOT);
}

void render_home()
{
    clean_root();
    draw_system_bar();
    draw_nav_bar();
    draw_home_icon_panel(selected_app());

    if (g_visible.empty()) {
        strong_label(g_root, "NO APPS", 122, 73, 130, 24, &lv_font_montserrat_20, 0xFFFFFF);
    } else {
        const int list_x = 116;
        const int y_pos[] = {25, 42, 60, 101, 118};
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
                         selected ? 24 : 16, fonts[row], colors[row],
                         selected ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
            if (selected) {
                strong_label(g_root, "V" + one_line(app.version.empty() ? "0" : app.version, 6),
                             264, y_pos[row] + 4, 52, 16, &lv_font_montserrat_14, 0x8B8B8B,
                             LV_LABEL_LONG_DOT);
                std::string author = app.author.empty() ? "unknown" : app.author;
                strong_label(g_root, author + ".",
                             list_x + 3, y_pos[row] + 23, 196, 12,
                             &lv_font_montserrat_10, 0xBDBDBD);
            }
        }
        std::string count = std::to_string(g_selected + 1) + "/" + std::to_string(g_visible.size());
        strong_label(g_root, count, 284, 136, 34, 15, &lv_font_montserrat_14, 0xFFFFFF,
                     LV_LABEL_LONG_DOT);
    }

    if (!g_status_message.empty()) {
        strong_label(g_root, one_line(g_status_message, 22), 122, 141, 156, 12,
                     &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    }
    box(120, 154, 198, 14, 0x333333, 0x333333, 0);
    strong_label(g_root, "ok : detail", 122, 155, 54, 12, &lv_font_montserrat_10,
                 0x43CF4D, LV_LABEL_LONG_DOT);
    strong_label(g_root, "tab : sort", 176, 155, 52, 12, &lv_font_montserrat_10,
                 0x168CE5, LV_LABEL_LONG_DOT);
    strong_label(g_root, "(" + sort_rule_label(g_sort_rule) + ")", 229, 155, 40, 12,
                 &lv_font_montserrat_10, 0x168CE5, LV_LABEL_LONG_DOT);
    strong_label(g_root, "r : reload", 270, 155, 48, 12, &lv_font_montserrat_10,
                 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_detail()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    StoreApp *app = selected_app();
    if (!app) {
        label(g_root, "No selected app", 10, 50, 304, 14, &lv_font_montserrat_12, 0xE6EDF3);
        return;
    }
    label(g_root, one_line(app->name + "  " + app->version, 30), 8, 24, 220, 15,
          &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
    label(g_root, "B Back", 270, 24, 44, 14, &lv_font_montserrat_10, 0xAECBFA);

    label(g_root, "State :", 10, 50, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, app->installed ? "Installed" : "Not installed", 62, 50, 105, 12,
          &lv_font_montserrat_10, app->installed ? 0xCCCC33 : 0xE6EDF3);
    label(g_root, "Size :", 176, 50, 42, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, app->size, 220, 50, 84, 12, &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);

    label(g_root, "Author:", 10, 66, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(app->author.empty() ? "-" : app->author, 36), 62, 66, 246, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(g_root, "Git   :", 10, 82, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(app->git_url.empty() ? "-" : app->git_url, 42), 62, 82, 246, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(g_root, "Image :", 10, 98, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(app->images.empty() ? "-" : app->images, 42), 62, 98, 246, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(g_root, "Deps  :", 10, 114, 48, 12, &lv_font_montserrat_10, 0x58A6FF);
    label(g_root, one_line(app->dependencies.empty() ? "-" : app->dependencies, 42), 62, 114, 246, 12,
          &lv_font_montserrat_10, 0xE6EDF3, LV_LABEL_LONG_DOT);
    label(g_root, one_line(app->description, 54), 10, 131, 300, 14,
          &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    if (g_job_running) {
        box(10, 144, 300, 5, 0x30363D, 0x30363D, 0);
        if (g_job_progress >= 0) {
            int fill = std::max(2, std::min(300, g_job_progress * 300 / 100));
            box(10, 144, fill, 5, 0xCCCC33, 0xCCCC33, 0);
        } else {
            int offset = static_cast<int>((lv_tick_get() / 120) % 260);
            box(10 + offset, 144, 40, 5, 0xCCCC33, 0xCCCC33, 0);
        }
        label(g_root, one_line(g_status_message.empty() ? "Working..." : g_status_message, 54),
              10, 153, 300, 12, &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    } else if (!g_status_message.empty()) {
        label(g_root, one_line(g_status_message, 54), 10, 153, 300, 12,
              &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    } else if (app->installed) {
        label(g_root, "R Run  U Uninstall  I Reinstall  B Back", 10, 153, 300, 12,
              &lv_font_montserrat_10, 0xCCCC33);
    } else {
        label(g_root, "I Install  B Back", 10, 153, 300, 12, &lv_font_montserrat_10, 0xCCCC33);
    }
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

    box(54, 119, 84, 17, 0x1A6A2A, 0x2EA043, 1);
    center_strong_label(g_root, "Y YES", 58, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    box(182, 119, 84, 17, 0x4B1F24, 0xF85149, 1);
    center_strong_label(g_root, "N NO", 186, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    std::string footer = g_confirm_action == "run" ? "This will leave AppStore and launch the app."
                                                    : "This will start the package operation.";
    center_strong_label(g_root, footer, 34, 149, 252, 12,
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
    center_strong_label(g_root, one_line(title, 26), 24, 70, 272, 16,
                        &lv_font_montserrat_12, 0xE6EDF3, LV_LABEL_LONG_DOT);

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

void render_loading()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Launching", 8, 24, 190, 15, &lv_font_montserrat_12, 0xFFFFFF);

    center_strong_label(g_root, upper_ascii(g_loading_title.empty() ? "LOADING" : g_loading_title),
                        32, 58, 256, 20, &lv_font_montserrat_14, 0xCCCC33, LV_LABEL_LONG_DOT);
    center_strong_label(g_root, one_line(g_loading_detail.empty() ? "Starting app..." : g_loading_detail, 30),
                        24, 84, 272, 16, &lv_font_montserrat_12, 0xE6EDF3, LV_LABEL_LONG_DOT);

    box(70, 111, 180, 8, 0x30363D, 0x30363D, 0, 2);
    int offset = static_cast<int>((lv_tick_get() / 80) % 132);
    box(70 + offset, 111, 48, 8, 0xCCCC33, 0xCCCC33, 0, 2);
    center_label(g_root, "Please wait...", 40, 138, 240, 12,
                 &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
}

void render_registry()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Registries", 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "B Back", 270, 24, 44, 14, &lv_font_montserrat_10, 0xAECBFA);

    if (g_registry_entries.empty()) {
        label(g_root, "No registries configured.", 10, 68, 300, 14,
              &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    } else {
        const RegistryEntry &entry = g_registry_entries[g_registry_selected];
        center_label(g_root, std::to_string(g_registry_selected + 1) + "/" +
                     std::to_string(g_registry_entries.size()), 132, 45, 56, 12,
                     &lv_font_montserrat_10, 0x8B949E, LV_LABEL_LONG_DOT);
        strong_label(g_root, "<", 8, 78, 12, 18, &lv_font_montserrat_20, 0xFF6A3D);
        strong_label(g_root, ">", 303, 78, 12, 18, &lv_font_montserrat_20, 0xFF6A3D);
        label(g_root, "NAME", 24, 48, 34, 12, &lv_font_montserrat_10, 0x58A6FF);
        strong_label(g_root, one_line(entry.name, 28), 24, 61, 210, 16,
                     &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
        label(g_root, entry.enabled ? "on" : "off", 242, 61, 28, 12, &lv_font_montserrat_10,
              entry.enabled ? 0x43CF4D : 0x6E7681, LV_LABEL_LONG_DOT);
        label(g_root, entry.count + " apps", 272, 61, 42, 12, &lv_font_montserrat_10,
              0xCCCC33, LV_LABEL_LONG_DOT);
        label(g_root, "URL", 24, 82, 34, 12, &lv_font_montserrat_10, 0x58A6FF);
        box(24, 96, 272, 38, 0x111923, 0x2A3A46, 1, 2);
        label(g_root, entry.url, 30, 100, 260, 30, &lv_font_montserrat_10, 0xE6EDF3,
              LV_LABEL_LONG_WRAP);
        if (!entry.error.empty()) {
            label(g_root, one_line(entry.error, 45), 24, 136, 272, 12, &lv_font_montserrat_10,
                  0xF85149, LV_LABEL_LONG_DOT);
        }
    }

    label(g_root, "A add  E edit  T on/off  D del  R sync", 10, 153, 300, 12,
          &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
}

void render_registry_edit()
{
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

void render()
{
    switch (g_screen) {
        case Screen::Home: render_home(); break;
        case Screen::Detail: render_detail(); break;
        case Screen::Confirm: render_confirm(); break;
        case Screen::Progress: render_progress(); break;
        case Screen::Loading: render_loading(); break;
        case Screen::Registry: render_registry(); break;
        case Screen::RegistryEdit: render_registry_edit(); break;
        case Screen::ShareCode: render_share_code(); break;
    }
}

void refresh_timer_cb(lv_timer_t *)
{
    if (g_screen == Screen::Home) {
        refresh_summary();
        render();
    }
}

void finish_backend_job(const std::string &out, const std::string &rc_text)
{
    if (g_job_pid > 0) {
        int status = 0;
        waitpid(g_job_pid, &status, 0);
        g_job_pid = -1;
    }
    bool ok = trim(rc_text) == "0" && out.find("ERROR") == std::string::npos;
    if (!ok) {
        g_status_message = one_line(backend_error_message(out), 54);
    } else if (g_job_action == "uninstall" || out.find("UNINSTALLED") != std::string::npos) {
        g_status_message = "Uninstalled";
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
    std::remove(g_job_output_path.c_str());
    std::remove(g_job_rc_path.c_str());
    refresh_summary();
    g_screen = Screen::Detail;
}

void poll_backend_job()
{
    if (!g_job_running || g_job_pending_start) return;
    uint32_t elapsed = lv_tick_elaps(g_job_start_tick) / 1000;
    std::string out = read_text_file(g_job_output_path);
    parse_job_progress(out);
    std::string detail = g_job_detail.empty() ? job_action_label(g_job_action) : g_job_detail;
    if (g_job_progress >= 0) {
        detail += " " + std::to_string(g_job_progress) + "%";
    }
    g_status_message = detail + " " + one_line(g_job_title, 16) + " " + std::to_string(elapsed) + "s";
    if (!file_exists(g_job_rc_path)) return;
    finish_backend_job(out, read_text_file(g_job_rc_path));
}

void job_timer_cb(lv_timer_t *)
{
    if (!g_job_running && !g_job_pending_start) return;
    if (g_job_pending_start && lv_tick_elaps(g_job_start_tick) >= kJobStartDelayMs) {
        g_job_pending_start = false;
        std::string flag = "--install";
        if (g_job_action == "reinstall") flag = "--reinstall";
        else if (g_job_action == "uninstall") flag = "--uninstall";

        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            int null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDIN_FILENO);
                if (null_fd > STDERR_FILENO) close(null_fd);
            }
            int out_fd = open(g_job_output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out_fd >= 0) {
                dup2(out_fd, STDOUT_FILENO);
                dup2(out_fd, STDERR_FILENO);
                if (out_fd > STDERR_FILENO) close(out_fd);
            }
            std::string cmd = backend_cmd(flag + " " + shell_quote(g_job_app_id));
            std::string wrapped = cmd + "; rc=$?; echo $rc > " + shell_quote(g_job_rc_path) + "; exit $rc";
            execlp("/bin/sh", "sh", "-c", wrapped.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }

        if (pid < 0) {
            g_job_running = false;
            g_status_message = "Unable to start operation";
            g_screen = Screen::Detail;
            render();
            return;
        }

        g_job_pid = pid;
        g_job_start_tick = lv_tick_get();
        g_job_detail = "Waiting for package output";
        g_status_message = job_action_label(g_job_action) + " " + one_line(g_job_title, 18) + "... 0s";
    }
    poll_backend_job();
    render();
}

void navigate_back()
{
    switch (g_screen) {
        case Screen::Home:
            break;
        case Screen::Detail:
            g_screen = Screen::Home;
            break;
        case Screen::Confirm:
            g_screen = Screen::Detail;
            break;
        case Screen::Progress:
            if (g_job_running || g_job_pending_start) {
                g_status_message = "Operation is still running";
            } else {
                g_screen = Screen::Detail;
            }
            break;
        case Screen::Loading:
            g_status_message = "Launch in progress";
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
    std::string message = sync_status_message(out);
    if (!message.empty()) {
        g_status_message = one_line(message, 54);
    } else if (out.find("SYNC\t0") != std::string::npos) {
        g_status_message = "No apps loaded";
    } else {
        g_status_message.clear();
    }
    refresh_summary();
    if (refresh_registries_after) refresh_registries();
}

void *sync_thread_main(void *)
{
    std::string out = run_capture(backend_cmd("--sync"));
    pthread_mutex_lock(&g_sync_mutex);
    g_sync_output = out;
    g_sync_done = true;
    g_sync_running = false;
    pthread_mutex_unlock(&g_sync_mutex);
    return nullptr;
}

void sync_catalog(bool refresh_registries_after = false)
{
    pthread_mutex_lock(&g_sync_mutex);
    if (g_sync_running) {
        pthread_mutex_unlock(&g_sync_mutex);
        g_status_message.clear();
        return;
    }
    g_sync_running = true;
    g_sync_done = false;
    g_sync_refresh_registries = refresh_registries_after;
    g_sync_output.clear();
    pthread_mutex_unlock(&g_sync_mutex);

    g_status_message.clear();
    pthread_t thread_id;
    if (pthread_create(&thread_id, nullptr, sync_thread_main, nullptr) != 0) {
        pthread_mutex_lock(&g_sync_mutex);
        g_sync_running = false;
        g_sync_done = false;
        pthread_mutex_unlock(&g_sync_mutex);
        g_status_message = "Unable to start sync";
        return;
    }
    pthread_detach(thread_id);
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
        render();
        return;
    }
    if (!done) return;
    apply_sync_output(out, refresh_registries_after);
    render();
}

void open_registry_screen()
{
    refresh_registries();
    g_screen = Screen::Registry;
}

void open_registry_add_screen()
{
    g_registry_edit_url.clear();
    g_registry_name_input.clear();
    g_registry_input = "https://cardputerzero.github.io/generated/registry-index.json";
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
    sort_apps(g_apps);
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
    refresh_summary();
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

RegistryEntry *selected_registry()
{
    if (g_registry_selected < 0 || g_registry_selected >= static_cast<int>(g_registry_entries.size())) return nullptr;
    return &g_registry_entries[g_registry_selected];
}

void add_registry_from_input()
{
    if (g_registry_name_input.empty() || g_registry_input.empty()) {
        g_status_message = "Name and URL required";
        return;
    }
    bool editing = !g_registry_edit_url.empty();
    g_status_message = editing ? "Updating registry..." : "Adding registry...";
    render();
    std::string out = editing ?
        run_capture(backend_cmd("--edit-registry " + shell_quote(g_registry_edit_url) + " " +
                                shell_quote(g_registry_input) + " --registry-name " + shell_quote(g_registry_name_input))) :
        run_capture(backend_cmd("--add-registry " + shell_quote(g_registry_input) +
                                " --registry-name " + shell_quote(g_registry_name_input)));
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(backend_error_message(out), 54);
    } else {
        std::string count = "";
        std::istringstream stream(out);
        std::string line;
        while (std::getline(stream, line)) {
            auto fields = split_tab(line);
            if (fields.size() >= 5 && fields[0] == "REGISTRY") {
                count = fields[1] == "UPDATED" && fields.size() >= 6 ? fields[5] : fields[4];
                break;
            }
        }
        g_status_message = (editing ? "Registry updated" : "Registry added") +
                           (count.empty() ? std::string() : " (" + count + " apps)");
        g_registry_edit_url.clear();
        g_screen = Screen::Registry;
    }
    refresh_registries();
    sync_catalog(true);
}

void toggle_selected_registry()
{
    RegistryEntry *entry = selected_registry();
    if (!entry) return;
    bool was_enabled = entry->enabled;
    std::string flag = entry->enabled ? "--disable-registry " : "--enable-registry ";
    std::string out = run_capture(backend_cmd(flag + shell_quote(entry->url)));
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(out, 44);
    } else {
        g_status_message = entry->enabled ? "Registry disabled" : "Registry enabled";
    }
    refresh_registries();
    refresh_summary();
    if (!was_enabled) sync_catalog(true);
}

void delete_selected_registry()
{
    RegistryEntry *entry = selected_registry();
    if (!entry) return;
    std::string out = run_capture(backend_cmd("--remove-registry " + shell_quote(entry->url)));
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(out, 44);
    } else {
        g_status_message = "Registry deleted";
    }
    g_registry_edit_url.clear();
    refresh_registries();
    refresh_summary();
}

void edit_selected_registry()
{
    RegistryEntry *entry = selected_registry();
    if (!entry) return;
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
            if (g_confirm_action == "uninstall") verb = "Uninstall ";
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

void start_confirm(const std::string &action)
{
    StoreApp *app = selected_app();
    if (!app) return;
    if (g_job_running) {
        g_status_message = "Operation already running";
        return;
    }
    g_confirm_action = action;
    if (action == "run") {
        if (!app->installed) {
            g_status_message = "Install this app before running";
            return;
        }
        g_confirm_lines = {"Run " + app->name, "Leave AppStore and launch the app.", "Use Esc in app to return."};
        g_screen = Screen::Confirm;
        return;
    }
    if (action == "uninstall") {
        g_confirm_lines = {"Uninstall " + app->name, "Remove installed Debian package.", "Disk free: " + g_free_space};
        g_screen = Screen::Confirm;
        return;
    }
    g_status_message = "Checking install plan...";
    render();
    std::string out = run_capture(backend_cmd("--plan " + shell_quote(app->id)));
    if (parse_plan(out)) {
        g_status_message.clear();
        g_screen = Screen::Confirm;
    } else {
        g_status_message = one_line(backend_error_message(out), 44);
    }
}

void start_backend_job(const std::string &action, StoreApp *app)
{
    if (!app) return;
    if (g_job_running) {
        g_status_message = "Operation already running";
        return;
    }

    std::string stamp = std::to_string(static_cast<unsigned long long>(time(nullptr))) +
                        "-" + std::to_string(static_cast<unsigned long long>(lv_tick_get()));
    std::string prefix = "/tmp/cardputerzero-appstore-" + stamp;
    g_job_output_path = prefix + ".out";
    g_job_rc_path = prefix + ".rc";
    std::remove(g_job_output_path.c_str());
    std::remove(g_job_rc_path.c_str());

    g_job_running = true;
    g_job_pending_start = true;
    g_job_action = action;
    g_job_app_id = app->id;
    g_job_title = app->name;
    g_job_stage.clear();
    g_job_detail = "Preparing package worker";
    g_job_progress = -1;
    g_job_pid = -1;
    g_job_start_tick = lv_tick_get();
    g_status_message = "Preparing " + one_line(app->name, 18) + "...";
    g_screen = Screen::Progress;
    render();
    lv_refr_now(nullptr);
}

void run_selected();

void execute_confirm()
{
    StoreApp *app = selected_app();
    if (!app || g_confirm_action.empty()) return;
    std::string action = g_confirm_action;
    g_confirm_action.clear();
    g_confirm_lines.clear();
    if (action == "run") {
        run_selected();
        return;
    }
    start_backend_job(action, app);
}

void run_selected()
{
    StoreApp *app = selected_app();
    if (!app) return;
    g_loading_title = "Launching";
    g_loading_detail = app->name;
    g_status_message.clear();
    g_screen = Screen::Loading;
    render();
    lv_refr_now(nullptr);

    std::string out = run_capture(backend_cmd("--run " + shell_quote(app->id)));
    if (out.find("RUNNING") != std::string::npos) {
        g_status_message = "Launched app";
        request_quit();
    } else {
        g_status_message = one_line(out.empty() ? "Run failed" : out, 44);
        g_screen = Screen::Detail;
    }
}

void handle_key(const KeyEvent &key)
{
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
        return;
    }
    if (key.release) return;
    switch (g_screen) {
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
            } else if (key.code == KEY_TAB) {
                cycle_sort_rule();
            } else if (key.code == KEY_ENTER && selected_app()) {
                g_screen = Screen::Detail;
            } else if (key_matches(key, 'r', KEY_R)) {
                sync_catalog();
            } else if (key_matches(key, 'a', KEY_A)) {
                open_registry_screen();
            } else if (key_matches(key, 's', KEY_S)) {
                open_share_code_screen();
            } else if (key_matches(key, 'q', KEY_Q)) {
                request_quit();
            }
            break;
        case Screen::Detail: {
            StoreApp *app = selected_app();
            if (key_matches(key, 'b', KEY_B)) {
                g_screen = Screen::Home;
            } else if (app && key_matches(key, 'i', KEY_I)) {
                start_confirm(app->installed ? "reinstall" : "install");
            } else if (app && app->installed && key_matches(key, 'u', KEY_U)) {
                start_confirm("uninstall");
            } else if (app && app->installed && key_matches(key, 'r', KEY_R)) {
                start_confirm("run");
            }
            break;
        }
        case Screen::Confirm:
            if (key_matches(key, 'b', KEY_B) || key_matches(key, 'n', KEY_N)) {
                g_screen = Screen::Detail;
                g_confirm_action.clear();
                g_confirm_lines.clear();
            } else if (key_matches(key, 'y', KEY_Y)) {
                execute_confirm();
            }
            break;
        case Screen::Progress:
            if (!g_job_running && !g_job_pending_start && key_matches(key, 'b', KEY_B)) {
                g_screen = Screen::Detail;
            }
            break;
        case Screen::Registry:
            if (key_matches(key, 'b', KEY_B)) {
                g_screen = Screen::Home;
            } else if (key_matches(key, 'a', KEY_A)) {
                open_registry_add_screen();
            } else if ((key.code == KEY_LEFT || key.code == KEY_Z || key.ch == 'z' || key.ch == '<') && !g_registry_entries.empty()) {
                g_registry_selected = g_registry_selected == 0 ?
                    static_cast<int>(g_registry_entries.size()) - 1 : g_registry_selected - 1;
            } else if ((key.code == KEY_RIGHT || key.code == KEY_C || key.ch == 'c' || key.ch == '>') && !g_registry_entries.empty()) {
                g_registry_selected = (g_registry_selected + 1) % static_cast<int>(g_registry_entries.size());
            } else if (key_matches(key, 'r', KEY_R)) {
                sync_catalog(true);
            } else if (key_matches(key, 't', KEY_T)) {
                toggle_selected_registry();
            } else if (key_matches(key, 'd', KEY_D)) {
                delete_selected_registry();
            } else if (key_matches(key, 'e', KEY_E)) {
                edit_selected_registry();
            } else if (key.code == KEY_ENTER) {
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
                if (g_share_code_input.empty() && key.ch == 's' &&
                    lv_tick_elaps(g_share_code_open_tick) < 250) {
                    break;
                }
                g_share_code_input.push_back(key.ch);
                g_share_code_message = "Enter opens the app detail page.";
            }
            break;
    }
    render();
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

#if LV_USE_SDL
uint32_t lv_key_to_linux(uint32_t key)
{
    switch (key) {
        case LV_KEY_UP: return KEY_UP;
        case LV_KEY_DOWN: return KEY_DOWN;
        case LV_KEY_RIGHT: return KEY_RIGHT;
        case LV_KEY_LEFT: return KEY_LEFT;
        case LV_KEY_NEXT: return KEY_TAB;
        case LV_KEY_ESC: return KEY_ESC;
        case LV_KEY_BACKSPACE: return KEY_BACKSPACE;
        case LV_KEY_ENTER: return KEY_ENTER;
        default: return key;
    }
}

void handle_lv_key_event(lv_event_t *event)
{
    uint32_t raw = lv_event_get_key(event);
    KeyEvent key;
    key.code = lv_key_to_linux(raw);
    if (raw >= 32 && raw < 127) key.ch = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
    handle_key(key);
}
#endif

int get_st7789v_fbdev(char *dev_path, size_t buf_size)
{
    if (!dev_path || buf_size == 0) return -1;
    FILE *fp = std::fopen("/proc/fb", "r");
    if (!fp) return -1;
    char line[256];
    int fb_num = -1;
    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strstr(line, "fb_st7789v") && std::sscanf(line, "%d", &fb_num) == 1) break;
    }
    std::fclose(fp);
    if (fb_num < 0) return -1;
    std::snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
    return 0;
}

#if LV_USE_EVDEV
int evdev_to_lv_key(uint16_t code)
{
    switch (code) {
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_DELETE: return LV_KEY_DEL;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_ENTER: return LV_KEY_ENTER;
        case KEY_TAB: return KEY_TAB;
        case KEY_HOME: return LV_KEY_HOME;
        case KEY_END: return LV_KEY_END;
        default: return code;
    }
}

void keypad_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
    pthread_mutex_lock(&keyboard_mutex);
    if (!STAILQ_EMPTY(&keyboard_queue)) {
        key_item *elm = STAILQ_FIRST(&keyboard_queue);
        STAILQ_REMOVE_HEAD(&keyboard_queue, entries);
        if (g_root) {
            lv_obj_send_event(g_root, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), elm);
        }
        data->key = evdev_to_lv_key(elm->key_code);
        data->state = static_cast<lv_indev_state_t>(elm->key_state);
        data->continue_reading = !STAILQ_EMPTY(&keyboard_queue);
        std::free(elm);
    }
    pthread_mutex_unlock(&keyboard_mutex);
}

void lv_linux_indev_init()
{
    const char *keyboard_device = getenv_default(
        "LV_LINUX_KEYBOARD_DEVICE",
        "/dev/input/by-path/platform-3f804000.i2c-event");
    pthread_t thread_id;
    pthread_create(&thread_id, nullptr, keyboard_read_thread, const_cast<char *>(keyboard_device));
    pthread_detach(thread_id);
    g_keyboard_indev = lv_indev_create();
    lv_indev_set_type(g_keyboard_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(g_keyboard_indev, keypad_read_cb);
}
#endif

#if LV_USE_LINUX_FBDEV
void lv_linux_disp_init()
{
    char fbdev[64] = {};
    const char *device = getenv_default("LV_LINUX_FBDEV_DEVICE", nullptr);
    if (!device && get_st7789v_fbdev(fbdev, sizeof(fbdev)) == 0) device = fbdev;
    if (!device) device = "/dev/fb0";
    lv_display_t *disp = lv_linux_fbdev_create();
    if (disp) lv_linux_fbdev_set_file(disp, device);
}

#if !LV_USE_EVDEV && !LV_USE_LIBINPUT
void lv_linux_indev_init() {}
#endif

#elif LV_USE_SDL
void lv_linux_disp_init()
{
    lv_display_t *disp = lv_sdl_window_create(kScreenWidth, kScreenHeight);
    lv_sdl_window_set_title(disp, "STORE");
}

void lv_linux_indev_init()
{
    lv_sdl_mouse_create();
    g_keyboard_indev = lv_sdl_keyboard_create();
}
#else
#error Unsupported display configuration
#endif

void build_ui()
{
    g_root = lv_screen_active();
    lv_obj_set_size(g_root, kScreenWidth, kScreenHeight);
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_root, handle_keyboard_event, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), nullptr);

    g_group = lv_group_create();
    lv_group_add_obj(g_group, g_root);
    lv_group_focus_obj(g_root);
    if (g_keyboard_indev) lv_indev_set_group(g_keyboard_indev, g_group);
}

}  // namespace

#ifndef APPSTORE_EMBEDDED
int main(int argc, char **argv)
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    g_app_dir = dirname_of(argv && argv[0] ? argv[0] : nullptr);
    g_script_path = resolve_script_path(g_app_dir);

    lv_init();
    lv_linux_disp_init();
    LV_EVENT_KEYBOARD = lv_event_register_id();
    lv_linux_indev_init();
    build_ui();
    refresh_summary();
    render();
    g_sync_timer = lv_timer_create(sync_timer_cb, 200, nullptr);
    sync_catalog();
    render();
    g_refresh_timer = lv_timer_create(refresh_timer_cb, 5000, nullptr);
    g_esc_hold_timer = lv_timer_create(esc_hold_timer_cb, 50, nullptr);
    g_job_timer = lv_timer_create(job_timer_cb, kJobPollIntervalMs, nullptr);

    while (!g_quit_requested) {
        lv_timer_handler();
        usleep(1000);
    }

    if (g_esc_hold_timer) lv_timer_delete(g_esc_hold_timer);
    if (g_refresh_timer) lv_timer_delete(g_refresh_timer);
    if (g_job_timer) lv_timer_delete(g_job_timer);
    if (g_sync_timer) lv_timer_delete(g_sync_timer);
    return 0;
}
#endif // APPSTORE_EMBEDDED
