#include "appstore_business.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include "cp0_lvgl_app.h"

namespace appstore {

namespace {

std::string g_backend_script_path = "appstore.py";
std::mutex g_backend_mutex;

std::vector<const char *> make_backend_argv(const std::vector<std::string> &args)
{
    static thread_local std::vector<std::string> storage;
    storage.clear();
    storage.reserve(args.size() + 2);
    storage.push_back("python3");
    storage.push_back(g_backend_script_path);
    storage.insert(storage.end(), args.begin(), args.end());

    std::vector<const char *> argv;
    argv.reserve(storage.size() + 1);
    for (const std::string &arg : storage) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    return argv;
}

std::string join_args(const std::vector<std::string> &args)
{
    std::string out;
    for (const auto &arg : args) {
        if (!out.empty()) out += " ";
        out += arg;
    }
    return out;
}

std::string preview_output(const std::string &output)
{
    std::string preview = one_line(output, 180);
    return preview.empty() ? "-" : preview;
}

std::string file_probe(const std::string &path)
{
    struct stat st {};
    if (path.empty()) return "empty-path";
    if (stat(path.c_str(), &st) != 0) return "missing";
    if (S_ISDIR(st.st_mode)) return "dir";
    return "file size=" + std::to_string(static_cast<long long>(st.st_size));
}

}  // namespace

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
        size_t keep = max_len > 3 ? max_len - 3 : max_len;
        while (keep > 0 && keep < value.size() &&
               (static_cast<unsigned char>(value[keep]) & 0xC0) == 0x80) {
            --keep;
        }
        value.resize(keep);
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

namespace {

size_t utf8_char_len(unsigned char ch)
{
    if ((ch & 0x80) == 0) return 1;
    if ((ch & 0xE0) == 0xC0) return 2;
    if ((ch & 0xF0) == 0xE0) return 3;
    if ((ch & 0xF8) == 0xF0) return 4;
    return 1;
}

std::string normalized_version(std::string value)
{
    value = trim(value);
    if (!value.empty() && (value[0] == 'v' || value[0] == 'V')) value.erase(value.begin());
    for (char &ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool versions_match(const std::string &available, const std::string &installed)
{
    std::string a = normalized_version(available);
    std::string b = normalized_version(installed);
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    return b.rfind(a + "-", 0) == 0 || b.rfind(a + "+", 0) == 0;
}

std::string app_sort_name(const StoreApp &app)
{
    return match_key(app.name);
}

bool app_title_less(const StoreApp &a, const StoreApp &b)
{
    std::string an = app_sort_name(a);
    std::string bn = app_sort_name(b);
    if (an != bn) return an < bn;
    return a.id < b.id;
}

}  // namespace

int utf8_display_width(const std::string &text)
{
    int width = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t len = utf8_char_len(ch);
        if (i + len > text.size()) len = 1;
        width += ch < 0x80 ? 1 : 2;
        i += len;
    }
    return width;
}

std::vector<std::string> wrap_display_text(std::string text, int max_width)
{
    for (char &ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    }
    text = trim(text);
    std::vector<std::string> lines;
    std::string current;
    int current_width = 0;
    size_t last_space_pos = std::string::npos;
    int width_at_last_space = 0;

    for (size_t i = 0; i < text.size();) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t len = utf8_char_len(ch);
        if (i + len > text.size()) len = 1;
        std::string token = text.substr(i, len);
        int token_width = ch < 0x80 ? 1 : 2;
        bool is_space = ch < 0x80 && std::isspace(ch);
        if (is_space && current.empty()) {
            i += len;
            continue;
        }
        current += token;
        current_width += token_width;
        if (is_space) {
            last_space_pos = current.size() - token.size();
            width_at_last_space = current_width - token_width;
        }
        if (current_width > max_width) {
            if (last_space_pos != std::string::npos && width_at_last_space > 0) {
                lines.push_back(trim(current.substr(0, last_space_pos)));
                current = trim(current.substr(last_space_pos + 1));
                current_width = utf8_display_width(current);
            } else {
                std::string overflow = token;
                current.resize(current.size() - token.size());
                if (!current.empty()) lines.push_back(trim(current));
                current = overflow;
                current_width = token_width;
            }
            last_space_pos = std::string::npos;
            width_at_last_space = 0;
        }
        i += len;
    }
    current = trim(current);
    if (!current.empty()) lines.push_back(current);
    if (lines.empty()) lines.push_back("-");
    return lines;
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
    if (missing.find("review-approved") != std::string::npos) return "Only approved apps can install";
    if (missing.find("deb-only") != std::string::npos) return "Only .deb packages are supported";
    if (missing.find("md5") != std::string::npos) return "Registry MD5 is required";
    if (missing.find("package-name") != std::string::npos) return "Deb package name is required";
    if (missing.find("package") != std::string::npos) return "Download URL is required";
    return "Install metadata is incomplete";
}

bool can_install_app(const StoreApp &app)
{
    return app.installable && app.review_status == "approved";
}

bool can_reinstall_app(const StoreApp &app)
{
    return app.installed && can_install_app(app);
}

bool can_upgrade_app(const StoreApp &app)
{
    return app.installed && can_install_app(app) && !versions_match(app.version, app.installed_version);
}

std::string review_label(const StoreApp &app)
{
    return app.review_status.empty() ? "not approved" : app.review_status;
}

std::string job_action_label(const std::string &action)
{
    if (action == "uninstall") return "Deleting";
    if (action == "upgrade") return "Upgrading";
    if (action == "reinstall") return "Reinstalling";
    return "Installing";
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
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
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
    return trim(value);
}

std::vector<std::string> split_csv_paths(const std::string &value)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : value) {
        if (ch == ',') {
            cur = trim(cur);
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    cur = trim(cur);
    if (!cur.empty()) out.push_back(cur);
    return out;
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

void sort_apps(std::vector<StoreApp> &apps, SortRule rule)
{
    std::stable_sort(apps.begin(), apps.end(), [rule](const StoreApp &a, const StoreApp &b) {
        switch (rule) {
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

void set_backend_script_path(std::string path)
{
    g_backend_script_path = std::move(path);
}

const std::string &backend_script_path()
{
    return g_backend_script_path;
}

std::string backend_capture(const std::vector<std::string> &args, int *rc)
{
    std::lock_guard<std::mutex> lock(g_backend_mutex);
    std::fprintf(stderr, "[AppStore UI] backend start script=%s (%s) args=%s\n",
                 g_backend_script_path.c_str(), file_probe(g_backend_script_path).c_str(),
                 join_args(args).c_str());
    std::vector<const char *> argv = make_backend_argv(args);
    std::vector<char> output(256 * 1024, '\0');
    int ret = cp0_process_capture_argv(argv.data(), output.data(), static_cast<int>(output.size()));
    std::string text(output.data());
    std::fprintf(stderr, "[AppStore UI] backend done rc=%d bytes=%zu script=%s args=%s preview=%s\n",
                 ret, text.size(), g_backend_script_path.c_str(), join_args(args).c_str(),
                 preview_output(text).c_str());
    if (rc) *rc = ret;
    return text;
}

std::string backend_capture_with_sudo(const std::vector<std::string> &args,
                                      const std::string &password,
                                      int *rc)
{
    std::lock_guard<std::mutex> lock(g_backend_mutex);
    std::fprintf(stderr, "[AppStore UI] backend sudo start script=%s (%s) args=%s password_len=%zu\n",
                 g_backend_script_path.c_str(), file_probe(g_backend_script_path).c_str(),
                 join_args(args).c_str(), password.size());
    const char *old_password = std::getenv("M5APPSTORE_SUDO_PASSWORD");
    std::string saved_password = old_password ? old_password : "";
    setenv("M5APPSTORE_SUDO_PASSWORD", password.c_str(), 1);

    std::vector<const char *> argv = make_backend_argv(args);
    std::vector<char> output(256 * 1024, '\0');
    int ret = cp0_process_capture_argv(argv.data(), output.data(), static_cast<int>(output.size()));

    if (old_password) setenv("M5APPSTORE_SUDO_PASSWORD", saved_password.c_str(), 1);
    else unsetenv("M5APPSTORE_SUDO_PASSWORD");
    std::string text(output.data());
    std::fprintf(stderr, "[AppStore UI] backend sudo done rc=%d bytes=%zu args=%s preview=%s\n",
                 ret, text.size(), join_args(args).c_str(), preview_output(text).c_str());
    if (rc) *rc = ret;
    return text;
}

SummaryData load_summary(SortRule rule)
{
    SummaryData data;
    int rc = -1;
    std::string output = backend_capture({"--summary"}, &rc);
    if (rc != 0) {
        data.warning = "Unable to load cache: " + one_line(backend_error_message(output), 80);
        std::fprintf(stderr, "[AppStore UI] summary failed rc=%d bytes=%zu warning=%s preview=%s\n",
                     rc, output.size(), data.warning.c_str(), preview_output(output).c_str());
        return data;
    }
    if (trim(output).empty()) {
        data.warning = "Unable to load cache: appstore.py returned no data";
        std::fprintf(stderr, "[AppStore UI] summary empty rc=%d bytes=%zu warning=%s\n",
                     rc, output.size(), data.warning.c_str());
        return data;
    }
    std::istringstream stream(output);
    std::string line;
    int meta_count = 0;
    int category_count = 0;
    int warning_count = 0;
    int app_count = 0;
    int ignored_count = 0;

    while (std::getline(stream, line)) {
        auto fields = split_tab(line);
        if (fields.empty()) continue;
        if (fields[0] == "META" && fields.size() >= 5) {
            ++meta_count;
            data.saw_meta = true;
            data.repo_status = fields[2];
            data.free_space = fields[3];
            data.root_path = fields[4];
        } else if (fields[0] == "CAT" && fields.size() >= 2) {
            ++category_count;
            data.categories.push_back(fields[1]);
        } else if (fields[0] == "WARN" && fields.size() >= 2) {
            ++warning_count;
            data.warning = fields[1];
        } else if (fields[0] == "APP" && fields.size() >= 13) {
            ++app_count;
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
            if (fields.size() >= 17) app.review_status = fields[16];
            if (fields.size() >= 18) app.installable = fields[17] == "1";
            if (fields.size() >= 19) app.installed_version = fields[18];
            data.apps.push_back(app);
        } else if (!trim(line).empty()) {
            ++ignored_count;
            std::fprintf(stderr, "[AppStore UI] summary ignored line fields=%zu text=%s\n",
                         fields.size(), one_line(line, 160).c_str());
        }
    }

    if (!data.apps.empty()) sort_apps(data.apps, rule);
    std::fprintf(stderr,
                 "[AppStore UI] summary parsed rc=%d bytes=%zu meta=%d cats=%d warn=%d apps=%d ignored=%d saw_meta=%d status=%s\n",
                 rc, output.size(), meta_count, category_count, warning_count, app_count, ignored_count,
                 data.saw_meta ? 1 : 0, data.repo_status.c_str());
    return data;
}

RegistryData load_registries(const std::string &fallback_registry_url)
{
    RegistryData data;
    data.region.registry_url = fallback_registry_url;

    std::string regions_output = backend_capture({"--regions"});
    std::istringstream regions_stream(regions_output);
    std::string region_line;
    while (std::getline(regions_stream, region_line)) {
        auto fields = split_tab(region_line);
        if (fields.size() >= 4 && fields[0] == "REGION") {
            data.region.code = fields[1];
            data.region.label = fields[2];
            data.region.registry_url = fields[3];
            if (fields.size() >= 5) data.region.active = fields[4];
            break;
        }
    }

    std::string output = backend_capture({"--registries"});
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
            if (fields.size() >= 9) entry.builtin = fields[8] == "1";
            if (fields.size() >= 10) entry.region = fields[9];
            std::string item = std::string(entry.enabled ? "on  " : "off ") + entry.status + "  " + entry.count + " apps";
            if (!entry.updated_at.empty()) item += "  " + entry.updated_at.substr(5, 5);
            if (!entry.error.empty()) {
                item += "  " + one_line(entry.error, 30);
            } else {
                item += "  " + entry.url;
            }
            data.entries.push_back(entry);
            data.lines.push_back(item);
        }
    }
    if (data.lines.empty()) {
        data.lines.push_back("not synced  0 apps  " + data.region.registry_url);
    }
    return data;
}

}  // namespace appstore
