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
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include <pthread.h>

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
constexpr const char *kDefaultRegistryUrl = "https://cardputerzero.github.io/generated/registry.json";

enum class Screen {
    Home,
    Detail,
    Confirm,
    SudoPassword,
    Progress,
    Registry,
    RegistryEdit,
    ShareCode,
    Search,
    Screenshots,
};

struct KeyEvent {
    uint32_t code = 0;
    uint32_t mods = 0;
    char ch = 0;
    bool release = false;
    bool repeated = false;
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
std::string g_sudo_password_input;
std::string g_sudo_message = "Enter sudo password for package operation.";
std::string g_sudo_action;
std::string g_sudo_app_id;
std::string g_sudo_app_title;
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
uint32_t g_sync_visible_until_tick = 0;
pthread_mutex_t g_summary_mutex = PTHREAD_MUTEX_INITIALIZER;
bool g_summary_running = false;
bool g_summary_done = false;
bool g_summary_pending = false;
SortRule g_summary_rule = SortRule::Default;
SummaryData g_summary_result;
cp0_wifi_status_t g_top_wifi_status = {};
cp0_battery_info_t g_top_battery_status = {};
uint32_t g_top_status_tick = 0;
uint32_t g_top_status_last_render_tick = 0;
int g_sync_anim_phase = -1;

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
    g_top_wifi_status = get_wifi_status();
    g_top_battery_status = cp0_battery_read();
    g_top_status_tick = lv_tick_get();
}

void refresh_summary_now();
void request_summary_refresh();

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

    refresh_summary_now();
    app = selected_app();
    if (app) return app;

    if (!g_apps.empty()) {
        select_default_category();
        rebuild_visible();
        g_selected = 0;
        app = selected_app();
        if (app) return app;
    }
    return nullptr;
}

bool open_detail_screen()
{
    if (ensure_selected_app()) {
        g_screen = Screen::Detail;
        return true;
    }
    g_status_message = "No app selected";
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

void refresh_registries()
{
    RegistryData registries = load_registries(g_region_registry_url);
    g_region_code = registries.region.code;
    g_region_label = registries.region.label;
    g_region_registry_url = registries.region.registry_url;
    g_region_active = registries.region.active;
    g_registry_entries = registries.entries;
    g_registry_lines = registries.lines;
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

void draw_system_bar()
{
    update_top_status_cache();
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
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(charging ? 0x2A2608 : 0x484847),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar, charging ? 70 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(battery_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(charging ? 0xFFD24A : 0x66CC33),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_bar, 170, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(battery_panel, lv_color_hex(charging ? 0xF3B51B : 0x000000),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_panel, charging ? 80 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    if (path.empty()) return false;
    g_render_image_sources.push_back(lvgl_posix_src(path));

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
    if (screenshots.empty()) return false;

    g_render_image_sources.push_back(lvgl_posix_src(screenshots[g_detail_image_index]));
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

void refresh_summary_now()
{
    std::fprintf(stderr, "[AppStore UI] refresh_summary_now start\n");
    apply_summary(load_summary(g_sort_rule));
    std::fprintf(stderr, "[AppStore UI] refresh_summary_now done apps=%zu visible=%zu cats=%zu status=%s\n",
                 g_apps.size(), g_visible.size(), g_categories.size(), g_repo_status.c_str());
}

void *summary_thread_main(void *arg)
{
    SortRule rule = *static_cast<SortRule *>(arg);
    delete static_cast<SortRule *>(arg);
    SummaryData summary = load_summary(rule);
    pthread_mutex_lock(&g_summary_mutex);
    g_summary_result = std::move(summary);
    g_summary_done = true;
    g_summary_running = false;
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
    pthread_mutex_unlock(&g_summary_mutex);

    pthread_t thread_id;
    auto *rule = new SortRule(rule_value);
    if (pthread_create(&thread_id, nullptr, summary_thread_main, rule) != 0) {
        delete rule;
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

    if (!g_status_message.empty()) {
        strong_label(g_root, one_line(g_status_message, 26), 106, 128, 172, 10,
                     &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
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
    if (!g_status_message.empty() && !g_job_running) {
        label(g_root, one_line(g_status_message, 54), 10, 122, 300, 12,
              &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
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

    box(54, 119, 84, 17, 0x1A6A2A, 0x2EA043, 1);
    center_strong_label(g_root, "Y YES", 58, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    box(182, 119, 84, 17, 0x4B1F24, 0xF85149, 1);
    center_strong_label(g_root, "N NO", 186, 122, 76, 12, &lv_font_montserrat_10, 0xFFFFFF);
    center_strong_label(g_root, "This will start the package operation.", 34, 149, 252, 12,
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
        label(g_root, "No registries configured.", 10, 82, 300, 14,
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
        label(g_root, entry.url, 30, 112, 260, 20, &lv_font_montserrat_10, 0xE6EDF3,
              LV_LABEL_LONG_WRAP);
        if (!entry.error.empty()) {
            label(g_root, one_line(entry.error, 45), 24, 136, 272, 12, &lv_font_montserrat_10,
                  0xF85149, LV_LABEL_LONG_DOT);
        }
    }

    std::string hint = region_focused ?
        "A add registry  Up/Down focus  < > region" :
        "A add registry  E edit  T toggle  D del  R sync";
    label(g_root, hint, 10, 153, 300, 12, &lv_font_montserrat_10,
          0xCCCC33, LV_LABEL_LONG_DOT);
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
        case Screen::Home: render_home(); break;
        case Screen::Detail: render_detail(); break;
        case Screen::Confirm: render_confirm(); break;
        case Screen::SudoPassword: render_sudo_password(); break;
        case Screen::Progress: render_progress(); break;
        case Screen::Registry: render_registry(); break;
        case Screen::RegistryEdit: render_registry_edit(); break;
        case Screen::ShareCode: render_share_code(); break;
        case Screen::Search: render_search(); break;
        case Screen::Screenshots: render_screenshots(); break;
    }
}

void refresh_timer_cb(lv_timer_t *)
{
    if (poll_summary_refresh()) {
        render();
    }
    bool battery_charging = g_top_battery_status.valid && (g_top_battery_status.flags & 1);
    if ((battery_charging &&
         lv_tick_elaps(g_top_status_last_render_tick) >= kBatteryChargeAnimRefreshMs) ||
        (!battery_charging && g_top_status_tick != 0 &&
         lv_tick_elaps(g_top_status_tick) >= kTopStatusRefreshMs)) {
        render();
    }
    if (g_screen == Screen::Screenshots && g_screenshots_overlay_visible &&
        lv_tick_elaps(g_screenshots_activity_tick) >= 2000) {
        g_screenshots_overlay_visible = false;
        render();
    }
}

void finish_backend_job(const std::string &out, const std::string &rc_text)
{
    bool ok = trim(rc_text) == "0" && out.find("ERROR") == std::string::npos;
    if (!ok) {
        g_status_message = one_line(backend_error_message(out), 54);
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
    g_screen = Screen::Detail;
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
    std::string out = backend_capture_with_sudo({flag, g_job_app_id}, g_job_sudo_password, &rc);
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
    if (refresh_registries_after) refresh_registries();
}

void *sync_thread_main(void *)
{
    std::fprintf(stderr, "[AppStore UI] sync thread start\n");
    std::string out = backend_capture({"--sync"});
    std::fprintf(stderr, "[AppStore UI] sync thread backend returned bytes=%zu preview=%s\n",
                 out.size(), one_line(out, 180).c_str());
    pthread_mutex_lock(&g_sync_mutex);
    g_sync_output = out;
    g_sync_done = true;
    g_sync_running = false;
    pthread_mutex_unlock(&g_sync_mutex);
    return nullptr;
}

void sync_catalog(bool refresh_registries_after = false)
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
    pthread_mutex_unlock(&g_sync_mutex);

    g_status_message = "Syncing catalog...";
    pthread_t thread_id;
    if (pthread_create(&thread_id, nullptr, sync_thread_main, nullptr) != 0) {
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
        int phase = static_cast<int>((lv_tick_get() / kSyncAnimRefreshMs) % 4);
        if (phase != g_sync_anim_phase) {
            g_sync_anim_phase = phase;
            render();
        }
        return;
    }
    if (!done) return;
    std::fprintf(stderr, "[AppStore UI] sync_timer done bytes=%zu\n", out.size());
    g_sync_anim_phase = -1;
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
    refresh_summary_now();
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
    refresh_summary_now();
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

void select_region(const std::string &region)
{
    if (region == g_region_code) {
        return;
    }
    std::string out = backend_capture({"--set-region", region});
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(backend_error_message(out), 54);
        return;
    }
    refresh_registries();
    g_status_message = "Region: " + g_region_label;
    request_summary_refresh();
    sync_catalog(true);
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
    bool editing = !g_registry_edit_url.empty();
    g_status_message = editing ? "Updating registry..." : "Adding registry...";
    render();
    std::string out = editing ?
        backend_capture({"--edit-registry", g_registry_edit_url, g_registry_input,
                         "--registry-name", g_registry_name_input}) :
        backend_capture({"--add-registry", g_registry_input,
                         "--registry-name", g_registry_name_input});
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
    std::string flag = entry->enabled ? "--disable-registry" : "--enable-registry";
    std::string out = backend_capture({flag, entry->url});
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(out, 44);
    } else {
        g_status_message = entry->enabled ? "Registry disabled" : "Registry enabled";
    }
    refresh_registries();
    request_summary_refresh();
    if (!was_enabled) sync_catalog(true);
}

void delete_selected_registry()
{
    RegistryEntry *entry = selected_registry();
    if (!entry) return;
    if (entry->builtin) {
        g_status_message = "Use region setting";
        return;
    }
    std::string out = backend_capture({"--remove-registry", entry->url});
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(out, 44);
    } else {
        g_status_message = "Registry deleted";
    }
    g_registry_edit_url.clear();
    refresh_registries();
    request_summary_refresh();
}

void edit_selected_registry()
{
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

void start_confirm(const std::string &action)
{
    StoreApp *app = selected_app();
    if (!app) return;
    if (g_job_running) {
        g_status_message = "Operation already running";
        return;
    }
    g_confirm_action = action;
    if (action == "uninstall") {
        g_confirm_lines = {"Delete " + app->name, "Remove installed Debian package.", "Disk free: " + g_free_space};
        g_screen = Screen::Confirm;
        return;
    }
    g_status_message = "Checking install plan...";
    render();
    std::string out = backend_capture({"--plan", app->id});
    if (parse_plan(out)) {
        g_status_message.clear();
        g_screen = Screen::Confirm;
    } else {
        g_status_message = one_line(backend_error_message(out), 44);
    }
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
                g_screen = Screen::Detail;
                g_confirm_action.clear();
                g_confirm_lines.clear();
            } else if (key_matches(key, 'y', KEY_Y)) {
                execute_confirm();
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

    g_app_dir = dirname_of(argv && argv[0] ? argv[0] : nullptr);
    set_backend_script_path(resolve_script_path(g_app_dir));
    std::fprintf(stderr, "[AppStore UI] ui_init argv0=%s app_dir=%s backend=%s\n",
                 argv && argv[0] ? argv[0] : "-", g_app_dir.c_str(), backend_script_path().c_str());

    init_runtime_fonts(g_app_dir);
    build_ui();
    update_top_status_cache(true);
    refresh_summary_now();
    render();
    g_sync_timer = lv_timer_create(sync_timer_cb, kSyncAnimRefreshMs, nullptr);
    sync_catalog();
    render();
    g_refresh_timer = lv_timer_create(refresh_timer_cb, 250, nullptr);
    g_esc_hold_timer = lv_timer_create(esc_hold_timer_cb, 50, nullptr);
    g_job_timer = lv_timer_create(job_timer_cb, kJobPollIntervalMs, nullptr);
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
}

bool ui_should_quit()
{
    return g_quit_requested != 0;
}
