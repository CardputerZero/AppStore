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
#include <fstream>
#include <sstream>
#include <string>
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

struct StoreApp {
    std::string id;
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
};

enum class Screen {
    Home,
    Detail,
    Confirm,
    Registry,
};

struct KeyEvent {
    uint32_t code = 0;
    char ch = 0;
    bool release = false;
    bool repeated = false;
};

lv_obj_t *g_root = nullptr;
lv_indev_t *g_keyboard_indev = nullptr;
lv_group_t *g_group = nullptr;
lv_timer_t *g_refresh_timer = nullptr;
lv_timer_t *g_esc_hold_timer = nullptr;
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
int g_category = 0;
int g_selected = 0;
Screen g_screen = Screen::Home;
std::string g_confirm_action;
std::vector<std::string> g_confirm_lines;
std::string g_registry_input = "https://cardputerzero.github.io/generated/registry-index.json";
uint32_t g_esc_press_tick = 0;
bool g_esc_pressed = false;
bool g_esc_long_consumed = false;

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

std::string first_csv(std::string value)
{
    size_t comma = value.find(',');
    if (comma != std::string::npos) value.resize(comma);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

bool file_exists(const std::string &path)
{
    if (path.empty()) return false;
    std::ifstream file(path);
    return file.good();
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
    std::string cat = g_categories.empty() ? "All" : g_categories[g_category];
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

void refresh_summary()
{
    std::string output = run_capture(backend_cmd("--summary"));
    std::istringstream stream(output);
    std::string line;
    std::vector<StoreApp> apps;
    std::vector<std::string> cats;

    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.empty()) continue;
        if (fields[0] == "META" && fields.size() >= 5) {
            g_repo_status = fields[2];
            g_free_space = fields[3];
            g_root_path = fields[4];
        } else if (fields[0] == "CAT" && fields.size() >= 2) {
            cats.push_back(fields[1]);
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
            apps.push_back(app);
        }
    }

    if (!cats.empty()) g_categories = cats;
    if (!apps.empty()) g_apps = apps;
    if (g_category >= static_cast<int>(g_categories.size())) g_category = 0;
    rebuild_visible();
}

void refresh_registries()
{
    g_registry_lines.clear();
    std::string output = run_capture(backend_cmd("--registries"));
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.size() >= 5 && fields[0] == "REG") {
            std::string item = fields[2] + "  " + fields[3] + " apps";
            if (!fields[4].empty()) item += "  " + fields[4].substr(5, 5);
            item += "  " + fields[1];
            g_registry_lines.push_back(item);
        }
    }
    if (g_registry_lines.empty()) {
        g_registry_lines.push_back("not synced  0 apps  " + g_registry_input);
    }
}

void clean_root()
{
    lv_obj_clean(g_root);
    g_render_image_sources.clear();
    g_render_image_sources.reserve(4);
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
    label(g_root, "ZERO", 5, 4, 58, 13, &lv_font_montserrat_12, 0xFFFFFF);

    box(206, 3, 40, 13, 0x333333, 0x333333, 0);
    label(g_root, current_time_text(), 210, 4, 34, 12, &lv_font_montserrat_10, 0xFFFFFF);

    box(248, 3, 30, 13, 0x333333, 0x333333, 0);
    box(253, 12, 5, 3, 0x00CCFF, 0x00CCFF, 0);
    box(260, 9, 5, 6, 0x00CCFF, 0x00CCFF, 0);
    box(267, 8, 5, 7, 0x00CCFF, 0x00CCFF, 0);
    box(274, 6, 3, 9, 0x4D4D4D, 0x4D4D4D, 0);

    box(280, 3, 38, 13, 0x333333, 0x333333, 0);
    label(g_root, "100%", 286, 4, 29, 12, &lv_font_montserrat_10, 0xFFFFFF);
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
    lv_obj_t *icon = lv_image_create(g_root);
    lv_image_set_scale(icon, 104);
    lv_image_set_src(icon, g_render_image_sources.back().c_str());
    lv_obj_update_layout(icon);

    int icon_w = lv_obj_get_width(icon);
    int icon_h = lv_obj_get_height(icon);
    if (icon_w <= 0) icon_w = 100;
    if (icon_h <= 0) icon_h = 100;

    constexpr int panel_center_x = 60;
    constexpr int panel_center_y = 76;
    lv_obj_set_pos(icon, panel_center_x - icon_w / 2, panel_center_y - icon_h / 2);
    return true;
#else
    (void)app;
    return false;
#endif
}

void draw_home_icon_panel(const StoreApp *app)
{
    box(20, 40, 81, 81, 0xFFFFFF, 0x4D4D4D, 1, 16, LV_OPA_TRANSP);
    if (!app) {
        label(g_root, "-", 49, 67, 22, 26, &lv_font_montserrat_20, 0x4D4D4D);
        return;
    }

    if (!draw_app_icon_image(*app)) {
        lv_obj_t *initial = label(g_root, app_initial(*app), 20, 61, 81, 28,
                                  &lv_font_montserrat_20, app->installed ? 0xCCCC33 : 0xFFFFFF);
        lv_obj_set_style_text_align(initial, LV_TEXT_ALIGN_CENTER, 0);
    }
    label(g_root, one_line(app->category, 10), 28, 102, 64, 12, &lv_font_montserrat_10, 0xCCCC33,
          LV_LABEL_LONG_DOT);
    if (app->installed) {
        box(25, 45, 30, 11, 0x333380, 0x333380, 0);
        label(g_root, "INST", 28, 45, 26, 11, &lv_font_montserrat_10, 0xFFFFFF);
    } else if (app->recommended) {
        box(25, 45, 28, 11, 0x333380, 0x333380, 0);
        label(g_root, "REC", 29, 45, 22, 11, &lv_font_montserrat_10, 0xFFFFFF);
    }
}

void draw_nav_bar()
{
    box(3, 147, 115, 19, 0x333380, 0x333380, 0);
    label(g_root, "<", 10, 150, 10, 12, &lv_font_montserrat_12, 0xFFFFFF);
    std::string cat = g_categories.empty() ? "All" : g_categories[g_category];
    lv_obj_t *cat_label = label(g_root, one_line(cat, 10), 25, 150, 70, 12,
                                &lv_font_montserrat_12, 0xFFFFFF, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(cat_label, LV_TEXT_ALIGN_CENTER, 0);
    label(g_root, ">", 102, 150, 10, 12, &lv_font_montserrat_12, 0xFFFFFF);
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
        label(g_root, "No apps", 112, 69, 196, 18, &lv_font_montserrat_20, 0xFFFFFF);
    } else {
        const int list_x = 112;
        const int y_pos[] = {29, 49, 68, 94, 114};
        const lv_font_t *fonts[] = {
            &lv_font_montserrat_10, &lv_font_montserrat_12, &lv_font_montserrat_20,
            &lv_font_montserrat_12, &lv_font_montserrat_10
        };
        const uint32_t colors[] = {0x6A6A6A, 0xA0A0A0, 0xFFFFFF, 0xA0A0A0, 0x6A6A6A};
        for (int row = 0; row < 5; ++row) {
            int visible_index = g_selected + row - 2;
            if (visible_index < 0 || visible_index >= static_cast<int>(g_visible.size())) continue;
            const StoreApp &app = g_apps[g_visible[visible_index]];
            bool selected = visible_index == g_selected;
            std::string mark = app.installed ? "* " : (app.recommended ? "+ " : "  ");
            std::string text = mark + one_line(app.name, selected ? 13 : 18);
            if (!app.version.empty() && !selected) text += " " + one_line(app.version, 5);
            if (selected) {
                box(list_x - 3, y_pos[row] - 1, 200, 25, 0xFFFFFF, 0xFFFFFF, 0, 0, LV_OPA_TRANSP);
            }
            label(g_root, text, list_x, y_pos[row], 148, selected ? 24 : 15,
                  fonts[row], colors[row], LV_LABEL_LONG_DOT);
            if (selected) {
                label(g_root, one_line(app.size, 8), 262, y_pos[row] + 7, 45, 12,
                      &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
            }
        }
    }

    label(g_root, "^", 57, 22, 12, 12, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "v", 57, 128, 12, 12, &lv_font_montserrat_12, 0xFFFFFF);
    box(121, 152, 15, 15, 0x333333, 0x333333, 0);
    label(g_root, "i", 127, 153, 6, 11, &lv_font_montserrat_10, 0xFFFFFF);
    label(g_root, "A Reg  R Sync", 142, 151, 104, 12, &lv_font_montserrat_10, 0x6A6A6A,
          LV_LABEL_LONG_DOT);

    if (!g_status_message.empty()) {
        label(g_root, one_line(g_status_message, 29), 142, 151, 174, 12,
              &lv_font_montserrat_10, 0xCCCC33, LV_LABEL_LONG_DOT);
    } else {
        label(g_root, "Free " + g_free_space, 248, 151, 66, 12,
              &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
    }
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
    if (app->installed) {
        label(g_root, "R Run   U Uninstall   I Reinstall", 10, 153, 300, 12,
              &lv_font_montserrat_10, 0xCCCC33);
    } else {
        label(g_root, "I Install", 10, 153, 300, 12, &lv_font_montserrat_10, 0xCCCC33);
    }
}

void render_confirm()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Confirm", 8, 24, 170, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "B Cancel", 264, 24, 50, 14, &lv_font_montserrat_10, 0xAECBFA);
    int y = 51;
    for (const auto &line : g_confirm_lines) {
        label(g_root, one_line(line, 48), 10, y, 300, 13, &lv_font_montserrat_10, 0xE6EDF3);
        y += 17;
        if (y > 132) break;
    }
    label(g_root, "I Confirm", 10, 153, 120, 12, &lv_font_montserrat_10, 0xCCCC33);
}

void render_registry()
{
    clean_root();
    draw_system_bar();
    box(0, 20, 320, 150, 0x0D1117, 0x0D1117, 0);
    box(0, 20, 320, 22, 0x1F6FEB, 0x1F6FEB, 0);
    label(g_root, "Registries", 8, 24, 180, 15, &lv_font_montserrat_12, 0xFFFFFF);
    label(g_root, "B Back", 270, 24, 44, 14, &lv_font_montserrat_10, 0xAECBFA);

    label(g_root, "URL", 10, 48, 28, 12, &lv_font_montserrat_10, 0x58A6FF);
    box(39, 45, 270, 20, 0x111923, 0x2A3A46, 1, 0);
    label(g_root, one_line(g_registry_input, 42), 44, 49, 260, 12, &lv_font_montserrat_10, 0xE6EDF3,
          LV_LABEL_LONG_DOT);

    int y = 72;
    for (const auto &line : g_registry_lines) {
        label(g_root, one_line(line, 50), 10, y, 300, 12, &lv_font_montserrat_10, 0xB8B8B8, LV_LABEL_LONG_DOT);
        y += 16;
        if (y > 132) break;
    }

    label(g_root, "Enter Add   R Sync   C Clear", 10, 153, 230, 12, &lv_font_montserrat_10, 0xCCCC33);
}

void render()
{
    switch (g_screen) {
        case Screen::Home: render_home(); break;
        case Screen::Detail: render_detail(); break;
        case Screen::Confirm: render_confirm(); break;
        case Screen::Registry: render_registry(); break;
    }
}

void refresh_timer_cb(lv_timer_t *)
{
    if (g_screen == Screen::Home) {
        refresh_summary();
        render();
    }
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
        case Screen::Registry:
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

void sync_catalog()
{
    g_status_message = "Syncing catalog...";
    render();
    std::string out = run_capture(backend_cmd("--sync"));
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(out, 44);
    } else if (out.find("SYNC\t0") != std::string::npos) {
        g_status_message = "Using built-in recommendations";
    } else {
        g_status_message = "Catalog synced";
    }
    refresh_summary();
}

void open_registry_screen()
{
    refresh_registries();
    g_screen = Screen::Registry;
}

void add_registry_from_input()
{
    if (g_registry_input.empty()) return;
    g_status_message = "Adding registry...";
    render();
    std::string out = run_capture(backend_cmd("--add-registry " + shell_quote(g_registry_input)));
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(out, 44);
    } else {
        g_status_message = "Registry added";
    }
    refresh_registries();
    refresh_summary();
}

bool parse_plan(const std::string &out)
{
    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.size() >= 8 && fields[0] == "PLAN") {
            g_confirm_lines.clear();
            g_confirm_lines.push_back((g_confirm_action == "uninstall" ? "Uninstall " : "Install ") + fields[2]);
            g_confirm_lines.push_back("Download/app size: " + fields[4]);
            g_confirm_lines.push_back("Disk free: " + fields[5]);
            g_confirm_lines.push_back("Dependencies: " + (fields[6].empty() ? "-" : fields[6]));
            g_confirm_lines.push_back("Missing deps: " + (fields[7].empty() ? "-" : fields[7]));
            return true;
        }
    }
    return false;
}

void start_confirm(const std::string &action)
{
    StoreApp *app = selected_app();
    if (!app) return;
    g_confirm_action = action;
    if (action == "uninstall") {
        g_confirm_lines = {"Uninstall " + app->name, "Remove installed APPLaunch files.", "Disk free: " + g_free_space};
        g_screen = Screen::Confirm;
        return;
    }
    std::string out = run_capture(backend_cmd("--plan " + shell_quote(app->id)));
    if (parse_plan(out)) {
        g_screen = Screen::Confirm;
    } else {
        g_status_message = one_line(out.empty() ? "Plan failed" : out, 44);
    }
}

void execute_confirm()
{
    StoreApp *app = selected_app();
    if (!app || g_confirm_action.empty()) return;
    std::string flag = "--install";
    if (g_confirm_action == "reinstall") flag = "--reinstall";
    else if (g_confirm_action == "uninstall") flag = "--uninstall";
    std::string out = run_capture(backend_cmd(flag + " " + shell_quote(app->id)));
    if (out.find("ERROR") != std::string::npos) {
        g_status_message = one_line(out, 44);
    } else if (out.find("UNINSTALLED") != std::string::npos) {
        g_status_message = "Uninstalled";
    } else if (out.find("INSTALLED") != std::string::npos) {
        g_status_message = "Installed. Return to launcher to test.";
    } else {
        g_status_message = one_line(out.empty() ? "Done" : out, 44);
    }
    refresh_summary();
    g_screen = Screen::Detail;
}

void run_selected()
{
    StoreApp *app = selected_app();
    if (!app) return;
    std::string out = run_capture(backend_cmd("--run " + shell_quote(app->id)));
    if (out.find("RUNNING") != std::string::npos) {
        g_status_message = "Launched app";
        request_quit();
    } else {
        g_status_message = one_line(out.empty() ? "Run failed" : out, 44);
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
            if (key.code == KEY_UP && g_selected > 0) {
                --g_selected;
            } else if (key.code == KEY_DOWN && g_selected + 1 < static_cast<int>(g_visible.size())) {
                ++g_selected;
            } else if ((key.code == KEY_LEFT || key.ch == '<') && !g_categories.empty()) {
                g_category = g_category == 0 ? static_cast<int>(g_categories.size()) - 1 : g_category - 1;
                rebuild_visible();
            } else if ((key.code == KEY_RIGHT || key.ch == '>') && !g_categories.empty()) {
                g_category = (g_category + 1) % static_cast<int>(g_categories.size());
                rebuild_visible();
            } else if (key.code == KEY_ENTER && selected_app()) {
                g_screen = Screen::Detail;
            } else if (key.ch == 'r') {
                sync_catalog();
            } else if (key.ch == 'a') {
                open_registry_screen();
            } else if (key.ch == 'q') {
                request_quit();
            }
            break;
        case Screen::Detail: {
            StoreApp *app = selected_app();
            if (key.ch == 'b') {
                g_screen = Screen::Home;
            } else if (app && key.ch == 'i') {
                start_confirm(app->installed ? "reinstall" : "install");
            } else if (app && app->installed && key.ch == 'u') {
                start_confirm("uninstall");
            } else if (app && app->installed && key.ch == 'r') {
                run_selected();
            }
            break;
        }
        case Screen::Confirm:
            if (key.ch == 'b') {
                g_screen = Screen::Detail;
            } else if (key.ch == 'i' || key.code == KEY_ENTER) {
                execute_confirm();
            }
            break;
        case Screen::Registry:
            if (key.ch == 'b') {
                g_screen = Screen::Home;
            } else if (key.ch == 'r') {
                sync_catalog();
                refresh_registries();
            } else if (key.ch == 'c') {
                g_registry_input.clear();
            } else if (key.code == KEY_BACKSPACE && !g_registry_input.empty()) {
                g_registry_input.pop_back();
            } else if (key.code == KEY_ENTER) {
                add_registry_from_input();
            } else if (key.ch >= 32 && key.ch <= 126 && g_registry_input.size() < 140) {
                g_registry_input.push_back(key.ch);
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
    lv_sdl_window_set_title(disp, "App Store");
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
    g_refresh_timer = lv_timer_create(refresh_timer_cb, 5000, nullptr);
    g_esc_hold_timer = lv_timer_create(esc_hold_timer_cb, 50, nullptr);

    while (!g_quit_requested) {
        lv_timer_handler();
        usleep(1000);
    }

    if (g_esc_hold_timer) lv_timer_delete(g_esc_hold_timer);
    if (g_refresh_timer) lv_timer_delete(g_refresh_timer);
    return 0;
}
