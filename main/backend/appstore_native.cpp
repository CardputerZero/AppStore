#include "appstore_native.hpp"
#include "appstore_process_entry.hpp"

// Native registry, networking, cache, and package-management backend.

#include "cp0_lvgl_app.h"
#include <hv/HttpClient.h>
#include <hv/hasync.h>
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <sys/file.h>
#include <pwd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <openssl/evp.h>
#include <openssl/sha.h>

namespace appstore {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char *kDefaultUrl = "https://cardputerzero.github.io/generated/registry.json";
constexpr const char *kDefaultName = "CardputerZero Hub";
constexpr const char *kCnUrl = "https://cardputer-zero-repo.oss-cn-shenzhen.aliyuncs.com/packages/cn/registry.json";
constexpr const char *kCnName = "CardputerZero Hub CN";

std::mutex g_state_mutex;
std::atomic<bool> g_sync_cancel{false};
std::atomic<bool> g_package_cancel{false};
std::mutex g_status_mutex;
json g_sync_status = {{"running", false}, {"cancel_requested", false}, {"url", ""},
                      {"detail", "Idle"}, {"percent", -1}, {"phase", "idle"}, {"updated_at", ""}};

std::string env_or(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value && value[0] ? value : fallback;
}

fs::path expand_home(std::string value)
{
    if (value.rfind("~/", 0) == 0) value = env_or("HOME", ".") + value.substr(1);
    return value;
}

fs::path state_dir() { return expand_home(env_or("M5APPSTORE_STATE_DIR", "~/.local/share/cardputerzero-appstore")); }
fs::path cache_dir() { return expand_home(env_or("M5APPSTORE_CACHE_DIR", "~/.cache/cardputerzero-appstore")); }
fs::path app_root() { return expand_home(env_or("M5APPSTORE_APP_ROOT", "/usr/share/APPLaunch")); }
fs::path config_path() { return state_dir() / "registries.json"; }
fs::path installed_path() { return state_dir() / "installed.json"; }
fs::path pending_path() { return state_dir() / "pending-package.json"; }
fs::path completed_path() { return state_dir() / "completed-package.json"; }

fs::path privileged_pending_path()
{
    if (const char *configured = std::getenv("M5APPSTORE_STATE_DIR"); configured && configured[0])
        return expand_home(configured) / "pending-package.json";
    if (const char *sudo_uid = std::getenv("SUDO_UID"); sudo_uid && sudo_uid[0]) {
        char *end = nullptr;
        const unsigned long value = std::strtoul(sudo_uid, &end, 10);
        if (end && *end == '\0') {
            if (const passwd *user = getpwuid(static_cast<uid_t>(value));
                user && user->pw_dir && user->pw_dir[0])
                return fs::path(user->pw_dir) / ".local/share/cardputerzero-appstore/pending-package.json";
        }
    }
    return pending_path();
}

std::string now_text()
{
    std::time_t now = std::time(nullptr);
    std::tm tm {};
    localtime_r(&now, &tm);
    char out[40] = {};
    std::strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%S%z", &tm);
    return out;
}

std::string field(const json &value)
{
    if (value.is_null()) return "";
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "1" : "0";
    if (value.is_number()) return value.dump();
    return value.dump();
}

std::string escape_tsv(const std::string &value)
{
    std::string out;
    for (char ch : value) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '\t') out += "\\t";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else out += ch;
    }
    return out;
}

template <typename... Args>
void emit(std::ostringstream &out, Args &&...args)
{
    bool first = true;
    auto append = [&](const auto &arg) {
        if (!first) out << '\t';
        first = false;
        out << escape_tsv(field(json(arg)));
    };
    (append(std::forward<Args>(args)), ...);
    out << '\n';
}

json read_json(const fs::path &path, json fallback = json::object())
{
    try {
        std::ifstream input(path);
        if (!input) return fallback;
        json value;
        input >> value;
        return value;
    } catch (...) {
        return fallback;
    }
}

void write_json(const fs::path &path, const json &value)
{
    fs::create_directories(path.parent_path());
    fs::path temporary = path;
    temporary += ".tmp";
    struct stat existing {};
    const bool preserve_mode = ::stat(path.c_str(), &existing) == 0;
    try {
        {
            std::ofstream output(temporary, std::ios::trunc);
            if (!output) throw std::runtime_error("unable to write " + path.string());
            output << std::setw(2) << value << '\n';
            output.flush();
            if (!output) throw std::runtime_error("unable to flush " + path.string());
        }
        const int temporary_fd = ::open(temporary.c_str(), O_RDONLY);
        if (temporary_fd < 0 || ::fsync(temporary_fd) != 0) {
            if (temporary_fd >= 0) ::close(temporary_fd);
            throw std::runtime_error("unable to sync " + path.string());
        }
        ::close(temporary_fd);
        if (preserve_mode && ::chmod(temporary.c_str(), existing.st_mode & 07777) != 0) {
            throw std::runtime_error("unable to preserve permissions for " + path.string());
        }
        fs::rename(temporary, path);
        const int directory_fd = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
        if (directory_fd >= 0) {
            ::fsync(directory_fd);
            ::close(directory_fd);
        }
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

void ensure_dirs()
{
    fs::create_directories(state_dir());
    fs::create_directories(cache_dir() / "icons");
    fs::create_directories(cache_dir() / "screenshots");
    fs::create_directories(cache_dir() / "downloads");
    for (const fs::path &path : {config_path(), installed_path(), pending_path(), completed_path()}) {
        const fs::path temporary = path.string() + ".tmp";
        std::error_code ignored;
        const auto modified = fs::last_write_time(temporary, ignored);
        if (!ignored && fs::file_time_type::clock::now() - modified > std::chrono::hours(1))
            fs::remove(temporary, ignored);
    }
}

std::string hex_digest(const unsigned char *data, size_t size)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned>(data[i]);
    return out.str();
}

std::string sha1_text(const std::string &value)
{
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(value.data()), value.size(), digest);
    return hex_digest(digest, sizeof(digest));
}

std::string md5_file(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("unable to read download");
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx || EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("unable to initialize md5 verification");
    }
    char buffer[1024 * 1024];
    while (input) {
        input.read(buffer, sizeof(buffer));
        if (input.gcount() > 0 &&
            EVP_DigestUpdate(ctx, buffer, static_cast<size_t>(input.gcount())) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("unable to update md5 verification");
        }
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_size) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("unable to finish md5 verification");
    }
    EVP_MD_CTX_free(ctx);
    return hex_digest(digest, digest_size);
}

std::vector<const char *> argv_view(const std::vector<std::string> &storage)
{
    std::vector<const char *> out;
    for (const auto &item : storage) out.push_back(item.c_str());
    out.push_back(nullptr);
    return out;
}

int run(const std::vector<std::string> &args)
{
    if (args.empty()) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
#ifdef __linux__
        const pid_t parent = getppid();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) _exit(128 + SIGTERM);
#endif
        auto view = argv_view(args);
        execvp(view[0], const_cast<char *const *>(view.data()));
        _exit(127);
    }
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited != pid) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

int run_cancellable(const std::vector<std::string> &args, const std::atomic<bool> &cancel)
{
    if (args.empty()) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setpgid(0, 0);
#ifdef __linux__
        const pid_t parent = getppid();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) _exit(128 + SIGTERM);
#endif
        auto view = argv_view(args);
        execvp(view[0], const_cast<char *const *>(view.data()));
        _exit(127);
    }
    setpgid(pid, pid);
    int status = 0;
    for (;;) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited < 0 && errno != EINTR) return -1;
        if (cancel.load()) {
            kill(-pid, SIGTERM);
            do { } while (waitpid(pid, &status, 0) < 0 && errno == EINTR);
            return 130;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

std::pair<int, std::string> capture(const std::vector<std::string> &args, size_t limit = 1024 * 1024)
{
    if (args.empty()) return {-1, ""};
    int pipes[2];
    if (pipe(pipes) != 0) return {-1, ""};
    pid_t pid = fork();
    if (pid == 0) {
        close(pipes[0]);
        dup2(pipes[1], STDOUT_FILENO);
        dup2(pipes[1], STDERR_FILENO);
        close(pipes[1]);
        auto view = argv_view(args);
        execvp(view[0], const_cast<char *const *>(view.data()));
        _exit(127);
    }
    close(pipes[1]);
    std::string output;
    char buffer[4096];
    ssize_t count = 0;
    while ((count = read(pipes[0], buffer, sizeof(buffer))) > 0)
        if (output.size() < limit) output.append(buffer, std::min<size_t>(count, limit - output.size()));
    close(pipes[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
}

std::string normalize_url(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    if (!value.empty() && value.find("://") == std::string::npos) value = "https://" + value;
    return value;
}

json builtin_registry(const std::string &region)
{
    bool cn = region == "CN";
    return {{"name", cn ? kCnName : kDefaultName}, {"url", cn ? kCnUrl : kDefaultUrl},
            {"enabled", true}, {"builtin", true}, {"region", cn ? "CN" : "default"}};
}

json load_config()
{
    json config = read_json(config_path());
    if (!config.is_object()) config = json::object();
    std::string region = config.value("region", "auto");
    if (region != "auto" && region != "CN" && region != "default") region = "auto";
    std::string active = config.value("active_region", "default");
    if (active != "CN") active = "default";
    json custom = json::array();
    if (config.contains("registries") && config["registries"].is_array()) {
        for (const auto &item : config["registries"]) {
            if (!item.is_object()) continue;
            std::string url = normalize_url(item.value("url", ""));
            if (url.empty() || url == kDefaultUrl || url == kCnUrl) continue;
            json copy = item;
            copy["url"] = url;
            copy["name"] = copy.value("name", url);
            copy["enabled"] = copy.value("enabled", true);
            custom.push_back(copy);
        }
    }
    config = {{"region", region}, {"active_region", active}, {"registries", json::array()}};
    config["registries"].push_back(builtin_registry(active));
    for (auto &item : custom) config["registries"].push_back(item);
    return config;
}

void save_config(const json &config) { write_json(config_path(), config); }

fs::path registry_cache(const std::string &url)
{
    return cache_dir() / ("registry-" + sha1_text(url).substr(0, 16) + ".json");
}

void clear_registry_cache()
{
    std::error_code error;
    for (const fs::path &folder : {cache_dir() / "icons", cache_dir() / "screenshots"}) {
        fs::remove_all(folder, error);
        if (error) throw std::runtime_error("unable to clear registry media cache: " + error.message());
        fs::create_directories(folder);
    }
    for (const auto &entry : fs::directory_iterator(cache_dir())) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("registry-", 0) == 0 && entry.path().extension() == ".json")
            fs::remove(entry.path());
    }
}

std::string url_root(const std::string &url)
{
    size_t slash = url.rfind('/');
    return slash == std::string::npos ? url : url.substr(0, slash + 1);
}

std::string absolute_url(const std::string &base, const std::string &ref)
{
    if (ref.rfind("http://", 0) == 0 || ref.rfind("https://", 0) == 0) return ref;
    if (!ref.empty() && ref[0] == '/') {
        size_t scheme = base.find("://");
        size_t host_end = scheme == std::string::npos ? std::string::npos : base.find('/', scheme + 3);
        return (host_end == std::string::npos ? base : base.substr(0, host_end)) + ref;
    }
    return url_root(base) + ref;
}

void update_status(bool running, const std::string &url, const std::string &detail, int percent,
                   const std::string &phase)
{
    std::lock_guard<std::mutex> lock(g_status_mutex);
    g_sync_status = {{"running", running}, {"cancel_requested", g_sync_cancel.load()}, {"url", url},
                     {"detail", detail}, {"percent", percent}, {"phase", phase}, {"updated_at", now_text()}};
}

void curl_download(const std::string &url, const fs::path &destination, bool resume = false,
                   const std::atomic<bool> *cancel = nullptr)
{
    fs::create_directories(destination.parent_path());
    std::vector<std::string> args = {"curl", "-fL", "--connect-timeout", "10", "--max-time", "900",
                                     "-A", "CardputerZero-AppStore/0.1"};
    if (resume) args.insert(args.end(), {"-C", "-"});
    args.insert(args.end(), {"-o", destination.string(), url});
    const int result = cancel ? run_cancellable(args, *cancel) : capture(args, 64 * 1024).first;
    if (result != 0) {
        if (cancel && cancel->load()) throw std::runtime_error("package operation cancelled");
        throw std::runtime_error("download failed (curl " + std::to_string(result) + "): " + url);
    }
}

struct HttpDownload {
    std::string url;
    fs::path destination;
};

struct HttpBatchState {
    std::mutex mutex;
    std::condition_variable changed;
    size_t pending = 0;
    std::vector<std::string> bodies;
    std::vector<std::string> errors;
};

void configure_proxy(hv::HttpClient &client)
{
    auto apply = [&](const char *name, bool https) {
        const char *raw = std::getenv(name);
        if (!raw || !raw[0]) return;
        std::string value(raw);
        size_t scheme = value.find("://");
        if (scheme != std::string::npos) value.erase(0, scheme + 3);
        size_t slash = value.find('/');
        if (slash != std::string::npos) value.resize(slash);
        size_t colon = value.rfind(':');
        if (colon == std::string::npos) return;
        const std::string host = value.substr(0, colon);
        const int port = std::atoi(value.substr(colon + 1).c_str());
        if (!host.empty() && port > 0) {
            if (https) client.setHttpsProxy(host.c_str(), port);
            else client.setHttpProxy(host.c_str(), port);
        }
    };
    apply("HTTP_PROXY", false);
    apply("HTTPS_PROXY", true);
    if (!std::getenv("HTTP_PROXY")) apply("http_proxy", false);
    if (!std::getenv("HTTPS_PROXY")) apply("https_proxy", true);
}

std::vector<bool> hv_download_batch(const std::vector<HttpDownload> &downloads,
                                    const std::atomic<bool> *cancel = nullptr)
{
    std::vector<bool> succeeded(downloads.size(), false);
    if (downloads.empty()) return succeeded;
    const bool proxy_configured = (std::getenv("HTTP_PROXY") && std::getenv("HTTP_PROXY")[0]) ||
        (std::getenv("HTTPS_PROXY") && std::getenv("HTTPS_PROXY")[0]) ||
        (std::getenv("http_proxy") && std::getenv("http_proxy")[0]) ||
        (std::getenv("https_proxy") && std::getenv("https_proxy")[0]);
    if (proxy_configured) {
        hv::async::startup(2, 6);
        std::vector<std::future<bool>> futures;
        futures.reserve(downloads.size());
        for (const auto &download : downloads) {
            futures.push_back(hv::async([download]() {
                try {
                    curl_download(download.url, download.destination);
                    return true;
                } catch (...) {
                    return false;
                }
            }));
        }
        for (size_t i = 0; i < futures.size(); ++i) {
            while (futures[i].wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
                if (cancel && cancel->load()) return succeeded;
            }
            succeeded[i] = futures[i].get();
        }
        return succeeded;
    }
    auto state = std::make_shared<HttpBatchState>();
    state->pending = downloads.size();
    state->bodies.resize(downloads.size());
    state->errors.resize(downloads.size());
    hv::HttpClient client;
    client.setTimeout(30);
    configure_proxy(client);

    for (size_t i = 0; i < downloads.size(); ++i) {
        auto request = std::make_shared<HttpRequest>();
        request->method = HTTP_GET;
        request->url = downloads[i].url;
        request->timeout = 30;
        request->connect_timeout = 10;
        request->headers["User-Agent"] = "CardputerZero-AppStore/0.1";
        request->headers["Cache-Control"] = "no-cache";
        const int submit = client.sendAsync(request, [state, i](const HttpResponsePtr &response) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (response && response->status_code >= 200 && response->status_code < 300)
                state->bodies[i] = response->body;
            else
                state->errors[i] = response ? "HTTP " + std::to_string(response->status_code) : "request failed";
            if (state->pending > 0) --state->pending;
            state->changed.notify_all();
        });
        if (submit != 0) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->errors[i] = http_client_strerror(submit);
            --state->pending;
        }
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    while (state->pending > 0) {
        if (cancel && cancel->load()) return succeeded;
        state->changed.wait_for(lock, std::chrono::milliseconds(50));
    }
    lock.unlock();
    for (size_t i = 0; i < downloads.size(); ++i) {
        if (!state->errors[i].empty()) continue;
        try {
            fs::create_directories(downloads[i].destination.parent_path());
            fs::path temporary = downloads[i].destination;
            temporary += ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(state->bodies[i].data(), static_cast<std::streamsize>(state->bodies[i].size()));
            output.close();
            if (!output || state->bodies[i].empty()) throw std::runtime_error("empty HTTP response");
            fs::rename(temporary, downloads[i].destination);
            succeeded[i] = true;
        } catch (...) {
            std::error_code ec;
            fs::remove(downloads[i].destination.string() + ".tmp", ec);
        }
    }
    return succeeded;
}

json request_json(const std::string &url)
{
    fs::path temp = cache_dir() / ("request-" + sha1_text(url).substr(0, 16) + ".json.tmp");
    const bool curl_transport = env_or("M5APPSTORE_HTTP_TRANSPORT", "") == "curl";
    if (curl_transport) {
        curl_download(url, temp);
    } else {
        auto result = hv_download_batch({{url, temp}}, &g_sync_cancel);
        if (result.empty() || !result[0]) throw std::runtime_error("download failed: " + url);
    }
    json value = read_json(temp, nullptr);
    std::error_code ec;
    fs::remove(temp, ec);
    if (!value.is_object()) throw std::runtime_error("invalid JSON response: " + url);
    return value;
}

std::vector<std::string> string_list(const json &value)
{
    std::vector<std::string> out;
    if (value.is_array()) {
        for (const auto &item : value)
            if (!item.is_null()) out.push_back(field(item));
    } else if (value.is_string()) {
        out.push_back(value.get<std::string>());
    }
    return out;
}

std::string dependencies_text(const json &app)
{
    std::vector<std::string> dependencies;
    auto append_unique = [&](const json &value) {
        for (const auto &dependency : string_list(value)) {
            if (std::find(dependencies.begin(), dependencies.end(), dependency) == dependencies.end())
                dependencies.push_back(dependency);
        }
    };
    if (app.contains("app") && app["app"].is_object())
        append_unique(app["app"].value("dependencies", json::array()));
    append_unique(app.value("dependencies", json::array()));
    std::ostringstream out;
    for (size_t index = 0; index < dependencies.size(); ++index) {
        if (index) out << ',';
        out << dependencies[index];
    }
    return out.str();
}

std::string clean_json_url(const std::string &url)
{
    size_t marker = url.find("_cz_appstore_ts=");
    if (marker == std::string::npos) return url;
    size_t separator = marker > 0 ? marker - 1 : marker;
    std::string cleaned = url.substr(0, separator);
    size_t tail = url.find('&', marker);
    if (tail != std::string::npos) cleaned += url.substr(tail);
    return cleaned;
}

std::string registry_site_root(const std::string &index_url)
{
    const std::string clean = clean_json_url(index_url);
    size_t generated = clean.find("/generated/");
    if (generated != std::string::npos) return clean.substr(0, generated + 1);
    return url_root(clean);
}

std::string full_registry_url(const std::string &index_url)
{
    const std::string clean = clean_json_url(index_url);
    size_t query = clean.find('?');
    const std::string path = clean.substr(0, query);
    if (fs::path(path).extension() == ".json") return clean;
    return (clean.empty() || clean.back() == '/' ? clean : clean + '/') + "registry.json";
}

std::string app_key(const json &app)
{
    for (const char *key : {"uuid", "share_code", "id", "title"}) {
        std::string value = app.value(key, "");
        if (!value.empty()) return value;
    }
    return "";
}

std::string locale()
{
    std::string fallback = env_or("LANG", "en");
    std::string value = env_or("M5APPSTORE_LOCALE", fallback.c_str());
    if (value.rfind("zh", 0) == 0) return value.find("TW") != std::string::npos ? "zh-TW" : "zh-CN";
    if (value.rfind("ja", 0) == 0) return "ja";
    return "en";
}

std::string localized(const json &app, const std::string &name)
{
    for (const char *container : {"i18n", "locales"}) {
        if (!app.contains(container) || !app[container].is_object()) continue;
        for (const auto &lang : {locale(), std::string("en"), std::string("zh-CN")}) {
            if (app[container].contains(lang) && app[container][lang].is_object()) {
                std::string value = app[container][lang].value(name, "");
                if (!value.empty()) return value;
            }
        }
    }
    return app.value(name, "");
}

json merge_apps(const std::vector<json> &records)
{
    std::map<std::string, json> merged;
    for (const auto &record : records) {
        json full_by_key = json::object();
        for (const auto &app : record.value("full", json::object()).value("apps", json::array()))
            if (app.is_object() && !app_key(app).empty()) full_by_key[app_key(app)] = app;
        std::set<std::string> indexed_keys;
        for (const auto &item : record.value("index", json::object()).value("apps", json::array())) {
            if (!item.is_object() || app_key(item).empty()) continue;
            const std::string key = app_key(item);
            json app = full_by_key.value(key, json::object());
            for (auto field = item.begin(); field != item.end(); ++field)
                if (!field.value().is_null() && field.value() != "" && field.value() != json::array())
                    app[field.key()] = field.value();
            app["_registry_name"] = record.value("name", "");
            app["_registry_url"] = record.value("url", "");
            app["_registry_status"] = record.value("status", "");
            app["_icon_local"] = record.value("icons", json::object()).value(key, "");
            app["_screenshots_local"] = record.value("screenshots", json::object()).value(key, json::array());
            merged[key] = app;
            indexed_keys.insert(key);
        }
        for (auto item : record.value("full", json::object()).value("apps", json::array())) {
            const std::string key = item.is_object() ? app_key(item) : "";
            if (key.empty() || indexed_keys.count(key) || merged.count(key)) continue;
            item["_registry_name"] = record.value("name", "");
            item["_registry_url"] = record.value("url", "");
            item["_registry_status"] = record.value("status", "");
            item["_icon_local"] = record.value("icons", json::object()).value(key, "");
            item["_screenshots_local"] = record.value("screenshots", json::object()).value(key, json::array());
            merged[key] = item;
        }
    }
    json out = json::array();
    for (auto &[key, app] : merged) out.push_back(app);
    std::sort(out.begin(), out.end(), [](const json &a, const json &b) {
        if (a.value("featured", false) != b.value("featured", false)) return a.value("featured", false);
        return localized(a, "title") < localized(b, "title");
    });
    return out;
}

std::vector<json> load_records(bool sync_if_empty);
std::string dispatch(const std::vector<std::string> &args, int &rc);
void select_region(json &config, const std::string &region);

std::string media_cache(const std::string &registry_url, const std::string &ref, const char *folder)
{
    if (ref.empty()) return "";
    std::string url = absolute_url(registry_site_root(registry_url), ref);
    std::string extension = fs::path(url.substr(0, url.find('?'))).extension().string();
    if (extension.empty() || extension.size() > 8) extension = ".png";
    fs::path destination = cache_dir() / folder / (sha1_text(url).substr(0, 16) + extension);
    if (!fs::exists(destination)) curl_download(url, destination);
    return destination.string();
}

struct ArtworkTask {
    std::string app;
    bool screenshot = false;
    HttpDownload download;
};

ArtworkTask artwork_task(const std::string &registry_url, const std::string &ref,
                         const std::string &app, bool screenshot)
{
    std::string media_url = absolute_url(registry_site_root(registry_url), ref);
    std::string extension = fs::path(media_url.substr(0, media_url.find('?'))).extension().string();
    if (extension.empty() || extension.size() > 8) extension = ".png";
    fs::path destination = cache_dir() / (screenshot ? "screenshots" : "icons") /
        (sha1_text(media_url).substr(0, 16) + extension);
    return {app, screenshot, {media_url, destination}};
}

json sync_registry(const json &source)
{
    std::string url = source.value("url", "");
    json record = {{"name", source.value("name", url)}, {"url", url}, {"status", "ok"},
                   {"synced_at", now_text()}, {"index", json::object()}, {"full", json::object()},
                   {"icons", json::object()}, {"screenshots", json::object()}};
    try {
        if (g_sync_cancel) throw std::runtime_error("sync cancelled");
        update_status(true, url, "Downloading registry index", 5, "registry");
        json index = request_json(url + (url.find('?') == std::string::npos ? "?" : "&") +
                                 "_cz_appstore_ts=" + std::to_string(std::time(nullptr)));
        record["index"] = index;
        std::string full_url = full_registry_url(url);
        try { record["full"] = full_url == url ? index : request_json(full_url); }
        catch (...) { record["full"] = json::object(); }

        json apps = record["full"].value("apps", json::array());
        if (apps.empty()) apps = index.value("apps", json::array());
        std::vector<ArtworkTask> pending_artwork;
        for (size_t i = 0; i < apps.size(); ++i) {
            if (g_sync_cancel) throw std::runtime_error("sync cancelled");
            const json &app = apps[i];
            if (!app.is_object() || app_key(app).empty()) continue;
            json assets = app.value("assets", json::object());
            std::string icon = app.value("icon", assets.value("icon", ""));
            if (!icon.empty()) {
                ArtworkTask task = artwork_task(url, icon, app_key(app), false);
                if (fs::exists(task.download.destination) && fs::file_size(task.download.destination) > 0)
                    record["icons"][task.app] = task.download.destination.string();
                else
                    pending_artwork.push_back(std::move(task));
            }
        }
        constexpr size_t kArtworkConcurrency = 6;
        for (size_t begin = 0; begin < pending_artwork.size(); begin += kArtworkConcurrency) {
            if (g_sync_cancel) throw std::runtime_error("sync cancelled");
            const size_t end = std::min(pending_artwork.size(), begin + kArtworkConcurrency);
            const int progress = 15 + static_cast<int>(75 * end / std::max<size_t>(1, pending_artwork.size()));
            update_status(true, url, "Caching app artwork", progress, "assets");
            std::vector<HttpDownload> downloads;
            for (size_t i = begin; i < end; ++i) downloads.push_back(pending_artwork[i].download);
            const auto results = hv_download_batch(downloads, &g_sync_cancel);
            const bool batch_failed = std::find(results.begin(), results.end(), false) != results.end();
            if (batch_failed) {
                if (g_sync_cancel) throw std::runtime_error("sync cancelled");
                try {
                    request_json(url + (url.find('?') == std::string::npos ? "?" : "&") +
                                 "_cz_appstore_probe=" + std::to_string(std::time(nullptr)));
                } catch (...) {
                    throw std::runtime_error("network lost while caching app artwork");
                }
            }
            for (size_t offset = 0; offset < results.size(); ++offset) {
                if (!results[offset]) continue;
                const ArtworkTask &task = pending_artwork[begin + offset];
                record["icons"][task.app] = task.download.destination.string();
            }
        }
        write_json(registry_cache(url), record);
    } catch (const std::exception &error) {
        json cached = read_json(registry_cache(url));
        if (cached.is_object() && cached.contains("index")) {
            record = cached;
            record["status"] = "cached";
            record["error"] = error.what();
        } else {
            record["status"] = "error";
            record["error"] = error.what();
            record["last_attempt_at"] = now_text();
        }
        write_json(registry_cache(url), record);
    }
    return record;
}

std::vector<json> sync_all()
{
    g_sync_cancel = false;
    update_status(true, "", "Loading registry configuration", 0, "config");
    json config = load_config();
    if (config.value("region", "auto") == "auto") {
        auto country = capture({"curl", "-fsSL", "--max-time", "5", "https://ipinfo.io/country"}, 128);
        std::string code = country.second;
        while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) code.pop_back();
        std::string active = code == "CN" ? "CN" : "default";
        if (active != config.value("active_region", "default")) {
            config["active_region"] = active;
            select_region(config, "auto");
            save_config(config);
        }
    }
    std::vector<json> records;
    size_t enabled = 0;
    for (const auto &item : config["registries"]) if (item.value("enabled", true)) ++enabled;
    size_t index = 0;
    for (const auto &item : config["registries"]) {
        if (!item.value("enabled", true)) continue;
        update_status(true, item.value("url", ""), "Syncing registry", static_cast<int>(100 * index / std::max<size_t>(1, enabled)), "registry");
        records.push_back(sync_registry(item));
        ++index;
        if (g_sync_cancel) break;
    }
    update_status(false, "", g_sync_cancel ? "Sync cancelled" : "Catalog synced",
                  g_sync_cancel ? -1 : 100, g_sync_cancel ? "cancelled" : "complete");
    return records;
}

std::vector<json> load_records(bool sync_if_empty)
{
    std::vector<json> records;
    const json config = load_config();
    for (const auto &source : config["registries"]) {
        if (!source.value("enabled", true)) continue;
        json record = read_json(registry_cache(source.value("url", "")));
        if (record.is_object() && (record.contains("index") || record.value("status", "") == "error"))
            records.push_back(record);
    }
    if (records.empty() && sync_if_empty) records = sync_all();
    return records;
}

std::string free_space()
{
    struct statvfs stat {};
    fs::path root = fs::exists(app_root()) ? app_root() : fs::path("/");
    if (statvfs(root.c_str(), &stat) != 0) return "-";
    double bytes = static_cast<double>(stat.f_bavail) * stat.f_frsize;
    std::ostringstream out;
    if (bytes >= 1024.0 * 1024 * 1024) out << std::fixed << std::setprecision(1) << bytes / (1024 * 1024 * 1024) << 'G';
    else out << static_cast<long long>(bytes / (1024 * 1024)) << 'M';
    return out.str();
}

std::string author(const json &app)
{
    if (app.contains("author") && app["author"].is_string())
        return app["author"].get<std::string>();
    if (app.contains("author") && app["author"].is_object()) {
        for (const char *key : {"display_name", "github"}) {
            const auto candidate = app["author"].find(key);
            std::string value = candidate != app["author"].end() && candidate->is_string()
                ? candidate->get<std::string>() : "";
            if (!value.empty()) return value;
        }
    }
    std::string repository;
    if (app.contains("source") && app["source"].is_object())
        repository = app["source"].value("repository", "");
    if (repository.empty())
        repository = app.value("source_repo", app.value("repository", app.value("git_url", "")));
    const std::string https_prefix = "https://github.com/";
    const std::string ssh_prefix = "git@github.com:";
    size_t owner_start = std::string::npos;
    if (repository.rfind(https_prefix, 0) == 0)
        owner_start = https_prefix.size();
    else if (repository.rfind(ssh_prefix, 0) == 0)
        owner_start = ssh_prefix.size();
    if (owner_start != std::string::npos) {
        const size_t owner_end = repository.find('/', owner_start);
        if (owner_end != std::string::npos && owner_end > owner_start)
            return repository.substr(owner_start, owner_end - owner_start);
    }
    return "";
}

json download_meta(const json &app) { return app.value("download", json::object()); }
std::string package_name(const json &app)
{
    json download = download_meta(app);
    return download.value("package", app.value("package", app.value("deb_package", "")));
}
std::string review(const json &app)
{
    if (app.contains("review") && app["review"].is_object()) return app.value("review_status", app["review"].value("status", ""));
    return app.value("review_status", "");
}

struct PackageState {
    bool installed = false;
    std::string version;
    bool known = false;
};

PackageState package_state(const std::string &package)
{
    if (package.empty()) return {false, "", true};
    auto [rc, output] = capture({"dpkg-query", "-W", "-f=${db:Status-Abbrev}\t${Version}", package}, 8192);
    if (rc != 0) {
        const bool absent = rc == 1 && output.find("no packages found matching") != std::string::npos;
        return {false, "", absent};
    }
    size_t tab = output.find('\t');
    std::string status = tab == std::string::npos ? output : output.substr(0, tab);
    std::string version = tab == std::string::npos ? "" : output.substr(tab + 1);
    while (!status.empty() && std::isspace(static_cast<unsigned char>(status.back()))) status.pop_back();
    while (!version.empty() && std::isspace(static_cast<unsigned char>(version.back()))) version.pop_back();
    const bool installed = status.size() >= 2 && status[1] == 'i';
    const bool absent = (status.size() == 2 || status.size() == 3) &&
        std::string("uihpr").find(status[0]) != std::string::npos && (status[1] == 'n' || status[1] == 'c');
    return {installed, version, installed || absent};
}

PackageState effective_package_state(const std::string &package)
{
    PackageState state = package_state(package);
    const json pending = read_json(pending_path());
    if (!pending.is_object() || pending.value("package", "") != package)
        return state;
    if (pending.value("helper_completed", false)) return state;
    const bool was_installed = pending.value("previously_installed", false);
    return {was_installed, was_installed ? pending.value("previous_version", "") : "", true};
}

bool package_state_unchanged(const json &pending, const PackageState &state)
{
    if (!state.known) return false;
    if (!pending.contains("previously_installed") || !pending["previously_installed"].is_boolean()) return false;
    if (!pending["previously_installed"].get<bool>()) return !state.installed;
    const std::string previous = pending.value("previous_version", "");
    return state.installed && !previous.empty() && state.version == previous;
}

std::vector<std::string> package_files(const std::string &package)
{
    auto result = capture({"dpkg-query", "-L", package}, 1024 * 1024);
    if (result.first != 0) throw std::runtime_error("unable to inspect installed package files");
    std::vector<std::string> files;
    std::istringstream lines(result.second);
    std::string line;
    while (std::getline(lines, line)) if (!line.empty() && line.front() == '/') files.push_back(line);
    return files;
}

std::string applaunch_exec(const json &app)
{
    if (app.contains("app") && app["app"].is_object() && app["app"].contains("applaunch") &&
        app["app"]["applaunch"].is_object())
        return app["app"]["applaunch"].value("exec", "");
    return "";
}

json installed_records()
{
    json value = read_json(installed_path());
    return value.is_object() ? value : json::object();
}

bool installed(const json &app)
{
    auto state = effective_package_state(package_name(app));
    if (!package_name(app).empty()) return state.installed;
    return installed_records().contains(app_key(app));
}

json find_app(const std::string &wanted)
{
    for (const auto &app : merge_apps(load_records(true))) {
        if (app_key(app) == wanted || app.value("share_code", "") == wanted ||
            app.value("title", "") == wanted || localized(app, "title") == wanted) return app;
    }
    return nullptr;
}

std::string summary_output()
{
    std::ostringstream out;
    auto records = load_records(false);
    json apps = merge_apps(records);
    size_t usable = 0, cached = 0, failed = 0;
    for (const auto &record : records) {
        std::string status = record.value("status", "");
        if (status == "ok" || status == "cached") ++usable;
        if (status == "cached") ++cached;
        if (status == "error") ++failed;
    }
    std::string status = cached ? std::to_string(apps.size()) + " apps/cache" :
        std::to_string(apps.size()) + " apps/" + std::to_string(usable) + " registries";
    emit(out, "META", 1, status, free_space(), app_root().string());
    if (failed) emit(out, "WARN", "Registry unavailable");
    else if (cached) emit(out, "WARN", "Registry offline; using cached catalog");
    const json pending = read_json(pending_path());
    if (pending.is_object() && !pending.empty())
        emit(out, "WARN", "Interrupted " + pending.value("action", std::string("package operation")) +
             " pending; retry it from app details");
    std::vector<std::string> categories = {"Recommended", "All"};
    for (const auto &app : apps) for (const auto &category : string_list(app.value("categories", json::array())))
        if (std::find(categories.begin(), categories.end(), category) == categories.end()) categories.push_back(category);
    for (const auto &category : categories) emit(out, "CAT", category);
    for (const auto &app : apps) {
        json download = download_meta(app);
        auto state = effective_package_state(package_name(app));
        std::vector<std::string> images;
        std::string icon = app.value("_icon_local", "");
        if (!icon.empty()) images.push_back(icon);
        for (const auto &shot : string_list(app.value("_screenshots_local", json::array()))) images.push_back(shot);
        std::ostringstream image_text;
        for (size_t i = 0; i < images.size(); ++i) { if (i) image_text << ','; image_text << images[i]; }
        std::string source;
        if (app.contains("source") && app["source"].is_object()) source = app["source"].value("repository", "");
        if (source.empty()) source = app.value("source_repo", app.value("repository", app.value("git_url", "")));
        std::string dependencies = dependencies_text(app);
        emit(out, "APP", app_key(app), localized(app, "title"), app.value("version", ""),
             string_list(app.value("categories", json::array())).empty() ? "Other" : string_list(app.value("categories", json::array())).front(),
             installed(app) ? "1" : "0", (app.value("featured", false) || review(app) == "approved") ? "1" : "0",
             field(download.value("size", json("online"))), localized(app, "summary").empty() ? localized(app, "description") : localized(app, "summary"),
             author(app), source, image_text.str(), dependencies, app.value("share_code", ""), app.value("_registry_name", ""),
             app.value("updated_at", app.value("published_at", "")), review(app), review(app) == "approved" ? "1" : "0", state.version);
    }
    return out.str();
}

std::string registries_output()
{
    std::ostringstream out;
    std::map<std::string, json> cached;
    for (const auto &record : load_records(false)) cached[record.value("url", "")] = record;
    const json config = load_config();
    for (const auto &source : config["registries"]) {
        std::string url = source.value("url", "");
        json record = cached.count(url) ? cached[url] : json::object();
        size_t count = record.value("index", json::object()).value("apps", json::array()).size();
        emit(out, "REG", url, record.value("status", "not synced"), count,
             record.value("synced_at", record.value("last_attempt_at", "")), record.value("error", ""),
             source.value("enabled", true) ? "1" : "0", source.value("name", url),
             source.value("builtin", false) ? "1" : "0", source.value("region", ""));
    }
    return out.str();
}

std::string regions_output()
{
    json config = load_config();
    std::string selected = config.value("region", "auto");
    std::string active = config.value("active_region", "default");
    std::ostringstream out;
    emit(out, "REGION", selected, selected == "auto" ? "Auto" : (active == "CN" ? "China" : "Default"),
         active == "CN" ? kCnUrl : kDefaultUrl, active);
    emit(out, "REGION_OPTION", "auto", "Auto", active == "CN" ? kCnUrl : kDefaultUrl, selected == "auto" ? "1" : "0");
    emit(out, "REGION_OPTION", "default", "Default", kDefaultUrl, selected == "default" ? "1" : "0");
    emit(out, "REGION_OPTION", "CN", "China", kCnUrl, selected == "CN" ? "1" : "0");
    return out.str();
}

void select_region(json &config, const std::string &region)
{
    std::string selected = region == "CN" ? "CN" : (region == "auto" ? "auto" : "default");
    std::string active = selected == "auto" ? config.value("active_region", "default") : selected;
    if (active != "CN") active = "default";
    json custom = json::array();
    for (const auto &item : config["registries"]) if (!item.value("builtin", false)) custom.push_back(item);
    config = {{"region", selected}, {"active_region", active}, {"registries", json::array()}};
    config["registries"].push_back(builtin_registry(active));
    for (const auto &item : custom) config["registries"].push_back(item);
}

std::string config_output(const json &config)
{
    std::ostringstream out;
    emit(out, "CONFIG", config.value("region", "auto"), config.value("active_region", "default"), config["registries"].size());
    size_t index = 0;
    for (const auto &item : config["registries"]) {
        emit(out, "CONFIG_REG", index++, item.value("name", item.value("url", "")), item.value("url", ""),
             item.value("enabled", true) ? "1" : "0", item.value("builtin", false) ? "1" : "0", item.value("region", ""));
    }
    return out.str();
}

std::string transaction_id()
{
    std::ostringstream value;
    value << std::hex << std::chrono::steady_clock::now().time_since_epoch().count() << '-' << getpid();
    return value.str();
}

fs::path deb_path(const json &app)
{
    std::string url = download_meta(app).value("url", "");
    std::string name = fs::path(url.substr(0, url.find('?'))).filename().string();
    if (name.size() < 4 || name.substr(name.size() - 4) != ".deb") name = sha1_text(url).substr(0, 16) + ".deb";
    return cache_dir() / "downloads" / (sha1_text(url).substr(0, 16) + "-" + name);
}

std::string desktop_path(const json &app);

void emit_pending_package_job(std::ostringstream &out, const json &pending)
{
    const std::string action = pending.value("action", "");
    const std::string package = pending.value("package", "");
    const std::string tx = pending.value("transaction_id", "");
    if (action == "uninstall") {
        emit(out, "PACKAGE_JOB", "uninstall", package, "0", "", tx, pending_path().string());
        return;
    }
    const json app = pending.value("app_snapshot", json::object());
    emit(out, "PACKAGE_JOB", "install", pending.value("deb_path", ""),
         action == "reinstall" ? "1" : "0", desktop_path(app), tx,
         pending_path().string(), applaunch_exec(app),
         "/usr/lib/" + package + "/" + package + "_zero_device",
         "/usr/bin/" + package, "/usr/lib/" + package + "/" + package);
}

std::string desktop_path(const json &app)
{
    if (app.contains("app") && app["app"].is_object() && app["app"].contains("applaunch") && app["app"]["applaunch"].is_object()) {
        std::string entry = app["app"]["applaunch"].value("desktop_entry", "");
        if (!entry.empty()) return (app_root() / entry).string();
    }
    std::string slug = app.value("share_code", localized(app, "title"));
    std::transform(slug.begin(), slug.end(), slug.begin(), [](unsigned char ch) { return std::isspace(ch) ? '-' : std::tolower(ch); });
    return (app_root() / "applications" / (slug + ".desktop")).string();
}

std::string deb_field(const fs::path &path, const std::string &name)
{
    auto result = capture({"dpkg-deb", "-f", path.string(), name}, 8192);
    if (result.first != 0) throw std::runtime_error("unable to inspect deb package");
    while (!result.second.empty() && std::isspace(static_cast<unsigned char>(result.second.back()))) result.second.pop_back();
    return result.second;
}

void lock_transaction(int &fd)
{
    ensure_dirs();
    fs::path lock = state_dir() / "package-transaction.lock";
    fd = ::open(lock.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) throw std::runtime_error("unable to open package transaction lock");
    const int timeout_seconds = std::max(1, std::atoi(env_or("M5APPSTORE_LOCK_TIMEOUT", "30").c_str()));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            close(fd); fd = -1;
            throw std::runtime_error("unable to lock package transaction");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            close(fd); fd = -1;
            throw std::runtime_error("package transaction is still busy; retry after the package manager exits");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void unlock_transaction(int fd) { if (fd >= 0) { flock(fd, LOCK_UN); close(fd); } }

std::string plan_output(const std::string &id, int &rc)
{
    std::ostringstream out;
    json app = find_app(id);
    if (!app.is_object()) { emit(out, "ERROR", "app not found", id); rc = 1; return out.str(); }
    json download = download_meta(app);
    std::vector<std::string> missing;
    if (review(app) != "approved") missing.push_back("review-approved");
    std::string url = download.value("url", "");
    if (url.empty()) missing.push_back("package");
    else if (download.value("type", "") != "deb" && (url.size() < 4 || url.substr(url.size() - 4) != ".deb")) missing.push_back("deb-only");
    if (download.value("md5", download.value("md5sum", "")).empty()) missing.push_back("md5");
    if (package_name(app).empty()) missing.push_back("package-name");
    if (access(app_root().c_str(), W_OK) != 0) missing.push_back("root-write");
    std::string missing_text;
    for (const auto &item : missing) { if (!missing_text.empty()) missing_text += ','; missing_text += item; }
    std::string deps = dependencies_text(app);
    emit(out, "PLAN", app_key(app), localized(app, "title"), app.value("version", ""),
         field(download.value("size", json("deb"))), free_space(), deps, missing_text);
    rc = (missing.empty() || (missing.size() == 1 && missing[0] == "root-write")) ? 0 : 1;
    return out.str();
}

void reconcile_pending();

std::string repair_package_transaction(const std::string &id, int &rc)
{
    std::ostringstream out;
    int fd = -1;
    try {
        reconcile_pending();
        lock_transaction(fd);
        json pending = read_json(pending_path());
        if (!pending.is_object() || pending.empty()) {
            emit(out, "PACKAGE_REPAIRED", id, "No pending transaction remained");
            rc = 0;
        } else if (pending.value("app_id", "") != id) {
            throw std::runtime_error("pending package transaction belongs to another app");
        } else if (package_state_unchanged(
                       pending, package_state(pending.value("package", "")))) {
            std::error_code error;
            if (!fs::remove(pending_path(), error) && error)
                throw std::runtime_error("unable to remove the invalid package transaction: " +
                                         error.message());
            emit(out, "PACKAGE_REPAIRED", id, "Invalid pending transaction removed");
            rc = 0;
        } else {
            pending["helper_completed"] = false;
            pending["helper_started"] = false;
            pending["recovery_reason"] = "Retrying interrupted package operation";
            pending.erase("helper_failed");
            pending.erase("helper_exit_code");
            pending.erase("helper_failed_at");
            write_json(pending_path(), pending);
            emit(out, "PACKAGE_REPAIRED", id,
                 "Interrupted transaction prepared for retry");
            rc = 0;
        }
    } catch (const std::exception &error) {
        emit(out, "ERROR", error.what(), 1);
        rc = 1;
    }
    unlock_transaction(fd);
    return out.str();
}

std::string prepare_package(const std::string &action, const std::string &id, int &rc)
{
    std::ostringstream out;
    int fd = -1;
    try {
        lock_transaction(fd);
        g_package_cancel = false;
        json app = find_app(id);
        if (!app.is_object()) throw std::runtime_error("app not found: " + id);
        json pending = read_json(pending_path());
        if (pending.is_object() && !pending.empty()) {
            const bool same_operation = pending.value("app_id", "") == app_key(app) &&
                pending.value("action", "") == action;
            if (!same_operation && package_state_unchanged(
                    pending, package_state(pending.value("package", "")))) {
                fs::remove(pending_path());
                pending = json::object();
            } else if (!same_operation) {
                emit(out, "PENDING_CONFLICT", pending.value("app_id", ""),
                     pending.value("action", ""), pending.value("package", ""));
                throw std::runtime_error("another package transaction is pending; finish or retry its original operation");
            }
        }
        if (pending.is_object() && !pending.empty()) {
            if (pending.value("helper_completed", false))
                throw std::runtime_error("package operation was applied but not finalized; refresh Store to recover it");
            if (action != "uninstall") {
                const fs::path saved_deb = pending.value("deb_path", "");
                const std::string expected = download_meta(app).value(
                    "md5", download_meta(app).value("md5sum", ""));
                if (saved_deb.empty() || !fs::is_regular_file(saved_deb) ||
                    expected.empty() || md5_file(saved_deb) != expected) {
                    const json download = download_meta(app);
                    const std::string url = download.value("url", "");
                    if (url.empty() || expected.empty())
                        throw std::runtime_error("pending package download cannot be recovered from registry metadata");
                    const fs::path destination = deb_path(app);
                    fs::path partial = destination.string() + ".parted";
                    emit(out, "PROGRESS", "download", 0, 0, -1,
                         "Recovering package download");
                    curl_download(url, partial, true, &g_package_cancel);
                    if (md5_file(partial) != expected) {
                        fs::remove(partial);
                        throw std::runtime_error("recovered package MD5 mismatch");
                    }
                    fs::rename(partial, destination);
                    if (deb_field(destination, "Package") != pending.value("package", ""))
                        throw std::runtime_error("recovered Debian package name does not match transaction");
                    pending["deb_path"] = destination.string();
                    pending["expected_md5"] = expected;
                    pending["expected_package_version"] = deb_field(destination, "Version");
                    write_json(pending_path(), pending);
                }
            }
            emit_pending_package_job(out, pending);
            rc = 0;
            unlock_transaction(fd);
            return out.str();
        }
        std::string package = package_name(app);
        if (package.empty()) throw std::runtime_error("deb package name missing");
        const auto previous_state = package_state(package);
        if (!previous_state.known)
            throw std::runtime_error("unable to determine current Debian package state");
        if ((action == "uninstall" || action == "reinstall" || action == "upgrade") &&
            !previous_state.installed)
            throw std::runtime_error("package is not currently installed");
        if (action == "install" && previous_state.installed)
            throw std::runtime_error("package is already installed; use reinstall or upgrade");
        std::string tx = transaction_id();
        pending = {{"schema_version", 2}, {"transaction_id", tx}, {"action", action},
                   {"app_id", app_key(app)}, {"package", package}, {"created_at", now_text()},
                   {"helper_completed", false}, {"previously_installed", previous_state.installed},
                   {"previous_version", previous_state.version}, {"expected_version", app.value("version", "")},
                   {"app_snapshot", app}};
        if (action == "uninstall") {
            write_json(pending_path(), pending);
            emit_pending_package_job(out, pending);
        } else {
            if (action != "install" && action != "reinstall" && action != "upgrade") throw std::runtime_error("unsupported package action");
            if (review(app) != "approved") throw std::runtime_error("only approved apps can be installed");
            json download = download_meta(app);
            std::string url = download.value("url", "");
            std::string expected = download.value("md5", download.value("md5sum", ""));
            if (url.empty() || expected.empty())
                throw std::runtime_error("package download URL or MD5 is missing");
            fs::path destination = deb_path(app);
            if (!fs::exists(destination) || md5_file(destination) != expected) {
                fs::path partial = destination.string() + ".parted";
                emit(out, "PROGRESS", "download", 0, 0, -1, "Downloading package");
                curl_download(url, partial, true, &g_package_cancel);
                if (g_package_cancel) throw std::runtime_error("package operation cancelled");
                if (md5_file(partial) != expected) { fs::remove(partial); throw std::runtime_error("md5 mismatch"); }
                fs::rename(partial, destination);
            }
            if (deb_field(destination, "Package") != package) throw std::runtime_error("downloaded deb package name does not match registry metadata");
            pending["deb_path"] = destination.string();
            pending["expected_md5"] = expected;
            pending["expected_package_version"] = deb_field(destination, "Version");
            write_json(pending_path(), pending);
            emit_pending_package_job(out, pending);
        }
        rc = 0;
    } catch (const std::exception &error) { emit(out, "ERROR", error.what(), 1); rc = 1; }
    unlock_transaction(fd);
    return out.str();
}

void rewrite_desktop(const fs::path &path, const std::vector<std::string> &candidates)
{
    if (!fs::exists(path)) return;
    const fs::path resolved = fs::canonical(path);
    const fs::path allowed = fs::weakly_canonical(app_root() / "applications");
    if (resolved.parent_path() != allowed || resolved.extension() != ".desktop")
        throw std::runtime_error("desktop entry is outside the APPLaunch applications directory");
    std::string executable;
    for (const auto &candidate : candidates) {
        std::string command = candidate.substr(0, candidate.find(' '));
        if (!command.empty() && fs::is_regular_file(command) && access(command.c_str(), X_OK) == 0) { executable = command; break; }
    }
    if (executable.empty()) return;
    std::ifstream input(path);
    std::vector<std::string> lines;
    std::string line;
    bool replaced = false;
    while (std::getline(input, line)) {
        if (line.rfind("Exec=", 0) == 0) { line = "Exec=" + executable; replaced = true; }
        lines.push_back(line);
    }
    if (!replaced) lines.push_back("Exec=" + executable);
    std::ofstream output(path, std::ios::trunc);
    for (const auto &item : lines) output << item << '\n';
}

int package_helper(const std::string &action, const std::string &value, bool reinstall,
                   bool force_overwrite,
                   const std::string &desktop, const std::vector<std::string> &execs,
                   const std::string &transaction, const std::string &pending_file)
{
    int fd = -1;
    fs::path path = pending_file.empty() ? pending_path() : fs::path(pending_file);
    try {
        if (geteuid() != 0) throw std::runtime_error("package helper requires root");
        if (fs::weakly_canonical(path) != fs::weakly_canonical(privileged_pending_path()))
            throw std::runtime_error("package pending path is not the Store transaction file");
        lock_transaction(fd);
        json pending = read_json(path);
        if (!pending.is_object() || pending.value("transaction_id", "") != transaction)
            throw std::runtime_error("package transaction does not match helper request");
        const std::string pending_action = pending.value("action", "");
        const std::string expected_helper_action = pending_action == "uninstall" ? "uninstall" : "install";
        const std::string expected_value = pending_action == "uninstall"
            ? pending.value("package", "") : pending.value("deb_path", "");
        if (action != expected_helper_action || value != expected_value ||
            reinstall != (pending_action == "reinstall"))
            throw std::runtime_error("package helper arguments do not match the pending transaction");
        if (pending.value("helper_completed", false))
            throw std::runtime_error("package helper transaction is already complete");
        std::vector<std::string> command;
        if (action == "install") {
            std::printf("PROGRESS\tverify\t0\t0\t-1\tVerifying package\n");
            std::fflush(stdout);
            if (!fs::is_regular_file(value))
                throw std::runtime_error("prepared Debian package is missing");
            const std::string expected_md5 = pending.value("expected_md5", "");
            if (expected_md5.empty() || md5_file(value) != expected_md5)
                throw std::runtime_error("prepared Debian package failed MD5 verification");
            if (deb_field(value, "Package") != pending.value("package", "") ||
                deb_field(value, "Version") != pending.value("expected_package_version", ""))
                throw std::runtime_error("prepared Debian package metadata changed after preparation");
            std::printf("PROGRESS\tdependencies\t0\t0\t-1\tChecking package dependencies\n");
            std::fflush(stdout);
            bool dependencies_ok = true;
            std::string dependencies;
            for (const char *field_name : {"Pre-Depends", "Depends"}) {
                auto value_result = capture({"dpkg-deb", "-f", value, field_name}, 64 * 1024);
                if (value_result.first != 0) dependencies_ok = false;
                else if (!value_result.second.empty()) { if (!dependencies.empty()) dependencies += ", "; dependencies += value_result.second; }
            }
            if (dependencies_ok && !dependencies.empty())
                dependencies_ok = capture({"dpkg-checkbuilddeps", "-d", dependencies, "/dev/null"}, 64 * 1024).first == 0;
            if (dependencies_ok) {
                command = {"dpkg"};
                if (force_overwrite) command.push_back("--force-overwrite");
                command.insert(command.end(), {"--install", value});
            }
            else {
                auto audit = capture({"dpkg", "--audit"}, 64 * 1024);
                if (audit.first != 0 || !audit.second.empty())
                    throw std::runtime_error("cannot resolve package dependencies while dpkg has unfinished packages");
                command = {"apt-get", "-y"};
                if (force_overwrite)
                    command.insert(command.end(), {"-o", "Dpkg::Options::=--force-overwrite"});
                if (reinstall) command.push_back("--reinstall");
                command.insert(command.end(), {"install", value});
            }
        } else if (action == "uninstall") command = {"dpkg", "--remove", value};
        else throw std::runtime_error("unsupported package helper action");
        pending["helper_started"] = true;
        pending["helper_started_at"] = now_text();
        pending.erase("helper_failed");
        pending.erase("helper_exit_code");
        pending.erase("helper_failed_at");
        write_json(path, pending);
        std::printf("PROGRESS\tpackage-manager\t0\t0\t-1\t%s package\n",
                    action == "install" ? "Installing" : "Removing");
        std::fflush(stdout);
        int result = run(command);
        if (result != 0) {
            pending["helper_failed"] = true;
            pending["helper_exit_code"] = result;
            pending["helper_failed_at"] = now_text();
            write_json(path, pending);
            if (package_state_unchanged(pending, package_state(pending.value("package", "")))) {
                fs::remove(path);
                std::fprintf(stderr, "WARNING\tfailed package command left installed package state unchanged\n");
            }
            throw std::runtime_error("package manager failed with exit code " + std::to_string(result));
        }
        const PackageState resulting_state = package_state(pending.value("package", ""));
        const std::string expected_version = pending.value("expected_package_version", "");
        if (!resulting_state.known ||
            (action == "uninstall" ? resulting_state.installed
                                   : (!resulting_state.installed || expected_version.empty() ||
                                      resulting_state.version != expected_version)))
            throw std::runtime_error("package manager exited successfully but package state verification failed");
        if (action == "install" && !desktop.empty()) rewrite_desktop(desktop, execs);
        pending["helper_completed"] = true;
        pending["helper_completed_at"] = now_text();
        write_json(path, pending);
        std::printf("PACKAGE_HELPER\t%s\t%s\n", action.c_str(), value.c_str());
        unlock_transaction(fd);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "ERROR\t%s\n", error.what());
        unlock_transaction(fd);
        return 1;
    }
}

void record_completed(const json &pending, const std::string &version)
{
    json completed = read_json(completed_path());
    if (!completed.is_object()) completed = json::object();
    std::string tx = pending.value("transaction_id", "");
    completed[tx] = {{"transaction_id", tx}, {"action", pending.value("action", "")},
                     {"app_id", pending.value("app_id", "")}, {"package", pending.value("package", "")},
                     {"version", version}, {"completed_at", now_text()}};
    while (completed.size() > 32) {
        auto oldest = completed.begin();
        for (auto item = completed.begin(); item != completed.end(); ++item)
            if (item.value().value("completed_at", "") < oldest.value().value("completed_at", "")) oldest = item;
        completed.erase(oldest);
    }
    write_json(completed_path(), completed);
}

std::string finalize_package(const std::string &action, const std::string &id, const std::string &tx, int &rc)
{
    std::ostringstream out;
    int fd = -1;
    try {
        lock_transaction(fd);
        json pending = read_json(pending_path());
        if (!pending.is_object() || pending.empty()) {
            json completed = read_json(completed_path());
            if (completed.is_object() && completed.contains(tx)) {
                const json &item = completed[tx];
                emit(out, "PACKAGE_RESULT", action, item.value("app_id", id), item.value("package", ""), item.value("version", ""));
                rc = 0; unlock_transaction(fd); return out.str();
            }
            throw std::runtime_error("no pending package transaction");
        }
        if (pending.value("transaction_id", "") != tx || pending.value("action", "") != action)
            throw std::runtime_error("pending package transaction does not match finalize request");
        if (!pending.value("helper_completed", false)) throw std::runtime_error("package helper completion was not recorded");
        std::string package = pending.value("package", "");
        auto state = package_state(package);
        json app = pending.value("app_snapshot", json::object());
        std::string key = app_key(app);
        json records = installed_records();
        if (action == "uninstall") {
            if (!state.known || state.installed) throw std::runtime_error("package is still installed after uninstall: " + package);
            records.erase(key); records.erase(id);
            write_json(installed_path(), records);
            record_completed(pending, "");
            fs::remove(pending_path());
            emit(out, "PROGRESS", "uninstall", 1, 1, 100, "Remove complete");
            emit(out, "UNINSTALLED", id);
            emit(out, "PACKAGE_RESULT", action, key, package, "");
        } else {
            if (!state.known || !state.installed) throw std::runtime_error("package is not installed after " + action + ": " + package);
            std::string expected = pending.value("expected_package_version", "");
            if (expected.empty() || state.version != expected) throw std::runtime_error(action + " did not install the expected package version");
            const std::vector<std::string> files = package_files(package);
            records[key] = {{"package", package}, {"version", state.version}, {"deb_path", pending.value("deb_path", "")},
                            {"installed_at", now_text()}, {"title", localized(app, "title")},
                            {"exec", applaunch_exec(app)}, {"files", files}};
            write_json(installed_path(), records);
            record_completed(pending, state.version);
            fs::remove(pending_path());
            emit(out, "PROGRESS", action == "upgrade" ? "upgrade" : "install", 1, 1, 100,
                 action == "upgrade" ? "Upgrade complete" : "Install complete");
            emit(out, action == "upgrade" ? "UPGRADED" : "INSTALLED", key, localized(app, "title"));
            emit(out, "PACKAGE_RESULT", action, key, package, state.version);
        }
        rc = 0;
    } catch (const std::exception &error) { emit(out, "ERROR", error.what(), 1); rc = 1; }
    unlock_transaction(fd);
    return out.str();
}

void reconcile_pending()
{
    int fd = -1;
    bool finalize = false;
    std::string action;
    std::string app_id;
    std::string tx;
    try {
        lock_transaction(fd);
        if (!fs::exists(pending_path())) {
            unlock_transaction(fd);
            return;
        }
        json pending = read_json(pending_path());
        if (!pending.is_object() || pending.empty()) {
            std::error_code ignored;
            fs::remove(pending_path(), ignored);
            unlock_transaction(fd);
            return;
        }
        action = pending.value("action", "");
        app_id = pending.value("app_id", "");
        tx = pending.value("transaction_id", "");
        const std::string package = pending.value("package", "");
        if ((action != "install" && action != "reinstall" && action != "upgrade" &&
             action != "uninstall") || app_id.empty() || tx.empty() || package.empty()) {
            std::error_code ignored;
            fs::remove(pending_path(), ignored);
            unlock_transaction(fd);
            return;
        }
        const PackageState state = package_state(package);
        const std::string expected = pending.value("expected_package_version", "");
        const bool terminal = state.known &&
            (action == "uninstall" ? !state.installed
                                   : state.installed && !expected.empty() && state.version == expected);
        const bool changed_from_previous = !pending.value("previously_installed", false) ||
            pending.value("previous_version", "") != state.version;
        const bool applied = terminal &&
            (action == "uninstall" ? pending.value("previously_installed", false)
                                   : changed_from_previous);
        if (pending.value("helper_completed", false)) {
            if (terminal) {
                finalize = true;
            } else if (state.known) {
                pending["helper_completed"] = false;
                pending["helper_started"] = false;
                pending["recovery_reason"] = "package state changed before finalization";
                write_json(pending_path(), pending);
            }
        } else if (applied) {
            pending["helper_completed"] = true;
            pending["recovered_after_interruption"] = true;
            pending["helper_completed_at"] = now_text();
            write_json(pending_path(), pending);
            finalize = true;
        }
        unlock_transaction(fd);
        fd = -1;
    } catch (...) {
        unlock_transaction(fd);
        return;
    }
    if (finalize) {
        int ignored = 0;
        finalize_package(action, app_id, tx, ignored);
    }
}

std::string legacy_package(const std::string &action, const std::string &id, int &rc)
{
    std::string prepared = prepare_package(action, id, rc);
    if (rc != 0) return prepared;
    json pending = read_json(pending_path());
    std::string helper_action = action == "uninstall" ? "uninstall" : "install";
    std::string value = action == "uninstall" ? pending.value("package", "") : pending.value("deb_path", "");
    json app = pending.value("app_snapshot", json::object());
    std::string package = pending.value("package", "");
    std::vector<std::string> candidates = {"/usr/lib/" + package + "/" + package + "_zero_device",
                                           "/usr/bin/" + package, "/usr/lib/" + package + "/" + package};
    if (package_helper(helper_action, value, action == "reinstall", false,
                       action == "uninstall" ? "" : desktop_path(app),
                       candidates, pending.value("transaction_id", ""), pending_path().string()) != 0) {
        rc = 1;
        return prepared + "ERROR\tpackage helper failed\t1\n";
    }
    return prepared + finalize_package(action, id, pending.value("transaction_id", ""), rc);
}

std::string fetch_screenshots(const std::string &wanted, int &rc)
{
    std::ostringstream out;
    bool found = false;
    size_t downloaded = 0;
    for (const auto &source : load_config().value("registries", json::array())) {
        const std::string registry_url = source.value("url", "");
        if (registry_url.empty()) continue;
        const fs::path path = registry_cache(registry_url);
        json record = read_json(path);
        if (!record.is_object()) continue;
        json matched;
        for (const char *collection : {"full", "index"}) {
            for (const auto &app : record.value(collection, json::object()).value("apps", json::array())) {
                if (app.is_object() && app_key(app) == wanted) { matched = app; break; }
            }
            if (matched.is_object()) break;
        }
        if (!matched.is_object()) continue;
        found = true;
        const json assets = matched.value("assets", json::object());
        const json refs = assets.contains("screenshots") ? assets["screenshots"]
                                                         : matched.value("screenshots", json::array());
        json local = json::array();
        for (const auto &ref : string_list(refs)) {
            ArtworkTask task = artwork_task(registry_url, ref, wanted, true);
            bool ok = fs::exists(task.download.destination) &&
                      fs::file_size(task.download.destination) > 0;
            if (!ok) {
                try { curl_download(task.download.url, task.download.destination); }
                catch (...) {}
                ok = fs::exists(task.download.destination) &&
                     fs::file_size(task.download.destination) > 0;
            }
            if (!ok) continue;
            local.push_back(task.download.destination.string());
            emit(out, "SCREENSHOT", wanted, task.download.destination.string());
            ++downloaded;
        }
        record["screenshots"][wanted] = std::move(local);
        write_json(path, record);
        break;
    }
    if (!found) {
        emit(out, "ERROR", "app not found", wanted);
        rc = 1;
    } else if (downloaded == 0) {
        emit(out, "SCREENSHOTS", wanted, 0);
    }
    return out.str();
}

std::string dispatch(const std::vector<std::string> &args, int &rc)
{
    rc = 0;
    std::ostringstream out;
    if (args.empty()) { emit(out, "ERROR", "missing command"); rc = 1; return out.str(); }
    const std::string &command = args[0];
    if (command == "--clear-registry-cache") {
        clear_registry_cache();
        emit(out, "CACHE_CLEARED", "registry");
        return out.str();
    }
    if (command == "--fetch-screenshots" && args.size() >= 2)
        return fetch_screenshots(args[1], rc);
    if (command == "--summary" || command == "--summary-sync-if-empty") {
        reconcile_pending();
        if (command == "--summary-sync-if-empty" && load_records(false).empty()) sync_all();
        return summary_output();
    }
    if (command == "--registries") return registries_output();
    if (command == "--regions") return regions_output();
    if (command == "--sync-status") {
        std::lock_guard<std::mutex> lock(g_status_mutex);
        emit(out, "STATUS", g_sync_status.value("running", false) ? "1" : "0",
             g_sync_status.value("cancel_requested", false) ? "1" : "0", g_sync_status.value("url", ""),
             g_sync_status.value("detail", ""), g_sync_status.value("percent", -1),
             g_sync_status.value("phase", ""), g_sync_status.value("updated_at", ""));
        return out.str();
    }
    if (command == "--sync") {
        auto records = sync_all(); size_t ok = 0, cached = 0, failed = 0, apps = 0;
        for (const auto &record : records) { std::string status = record.value("status", ""); if (status == "ok") ++ok; else if (status == "cached") ++cached; else ++failed; apps += record.value("index", json::object()).value("apps", json::array()).size(); }
        const std::string message = g_sync_cancel ? "Sync cancelled" :
            (ok == 0 ? "No registry could be reached" : "Catalog synced");
        emit(out, "SYNC", ok, cached, failed, apps, message);
        if (g_sync_cancel) rc = 1;
        return out.str();
    }
    if (command == "--registry-config") return config_output(load_config());
    if (command == "--replace-registry-config" && args.size() >= 2) {
        try {
            json incoming = json::parse(args[1]); json config = load_config();
            config["region"] = incoming.value("region", "auto"); config["active_region"] = incoming.value("active_region", "default");
            config["registries"] = incoming.value("registries", json::array()); select_region(config, config.value("region", "auto"));
            save_config(config); return config_output(config);
        } catch (const std::exception &error) { emit(out, "ERROR", std::string("invalid registry config: ") + error.what()); rc = 1; return out.str(); }
    }
    if (command == "--set-region" && args.size() >= 2) {
        if (args[1] != "auto" && args[1] != "default" && args[1] != "CN") { emit(out, "ERROR", "unknown region", args[1]); rc = 1; return out.str(); }
        json config = load_config(); select_region(config, args[1]); save_config(config);
        emit(out, "REGION", config.value("region", "auto"), args[1] == "CN" ? "China" : (args[1] == "auto" ? "Auto" : "Default"),
             config.value("active_region", "default") == "CN" ? kCnUrl : kDefaultUrl, config.value("active_region", "default")); return out.str();
    }
    if (command == "--add-registry" && args.size() >= 2) {
        json config = load_config(); std::string url = normalize_url(args[1]); std::string name = url;
        for (size_t i = 2; i + 1 < args.size(); ++i) if (args[i] == "--registry-name") name = args[i + 1];
        if (url == kDefaultUrl || url == kCnUrl) { emit(out, "ERROR", "use region selection for built-in registries", url); rc = 1; return out.str(); }
        for (const auto &item : config["registries"]) if (item.value("url", "") == url) { emit(out, "ERROR", "registry already exists", url); rc = 1; return out.str(); }
        json record = sync_registry({{"name", name}, {"url", url}, {"enabled", true}});
        if (record.value("status", "") == "error") { emit(out, "ERROR", record.value("error", "registry unavailable"), url); rc = 1; return out.str(); }
        config["registries"].push_back({{"name", name}, {"url", url}, {"enabled", true}}); save_config(config);
        emit(out, "REGISTRY", "ADDED", url, record.value("status", ""), record.value("index", json::object()).value("apps", json::array()).size(), name); return out.str();
    }
    if ((command == "--remove-registry" || command == "--enable-registry" || command == "--disable-registry") && args.size() >= 2) {
        json config = load_config(); std::string url = normalize_url(args[1]); bool found = false; json entries = json::array();
        for (auto item : config["registries"]) {
            if (item.value("url", "") == url) { found = true; if (item.value("builtin", false)) { emit(out, "ERROR", "registry is managed by region selection", url); rc = 1; return out.str(); }
                if (command == "--remove-registry") continue; item["enabled"] = command == "--enable-registry"; }
            entries.push_back(item);
        }
        if (!found) { emit(out, "ERROR", "registry not found", url); rc = 1; return out.str(); }
        config["registries"] = entries; save_config(config); emit(out, "REGISTRY", command == "--remove-registry" ? "REMOVED" : (command == "--enable-registry" ? "ENABLED" : "DISABLED"), url); return out.str();
    }
    if (command == "--edit-registry" && args.size() >= 3) {
        std::string old_url = normalize_url(args[1]), new_url = normalize_url(args[2]), name = new_url; json config = load_config(); bool found = false;
        for (size_t i = 3; i + 1 < args.size(); ++i) if (args[i] == "--registry-name") name = args[i + 1];
        json record = sync_registry({{"name", name}, {"url", new_url}, {"enabled", true}});
        if (record.value("status", "") == "error") { emit(out, "ERROR", record.value("error", "registry unavailable"), new_url); rc = 1; return out.str(); }
        for (auto &item : config["registries"]) if (item.value("url", "") == old_url && !item.value("builtin", false)) { item["url"] = new_url; item["name"] = name; found = true; }
        if (!found) { emit(out, "ERROR", "registry not found", old_url); rc = 1; return out.str(); }
        save_config(config); emit(out, "REGISTRY", "UPDATED", old_url, new_url, record.value("status", ""), record.value("index", json::object()).value("apps", json::array()).size(), name); return out.str();
    }
    if (command == "--plan" && args.size() >= 2) return plan_output(args[1], rc);
    if (command == "--repair-package-transaction" && args.size() >= 2)
        return repair_package_transaction(args[1], rc);
    if (command == "--prepare-package" && args.size() >= 3) return prepare_package(args[1], args[2], rc);
    if (command == "--finalize-package" && args.size() >= 4) return finalize_package(args[1], args[2], args[3], rc);
    if ((command == "--install" || command == "--reinstall" || command == "--upgrade" || command == "--uninstall") && args.size() >= 2) {
        if (geteuid() != 0) { emit(out, "ERROR", "legacy package operations require root", 1); rc = 1; return out.str(); }
        return legacy_package(command.substr(2), args[1], rc);
    }
    emit(out, "ERROR", "unknown native backend command", command); rc = 1; return out.str();
}

}  // namespace

void native_request_sync_cancel() { g_sync_cancel = true; }
void native_request_package_cancel() { g_package_cancel = true; }

std::string native_backend_capture(const std::vector<std::string> &args, int *rc)
{
    ensure_dirs();
    int result = 0;
    std::string output;
    try { output = dispatch(args, result); }
    catch (const std::exception &error) {
        std::ostringstream text;
        emit(text, "ERROR", error.what(), 1);
        output = text.str();
        result = 1;
    }
    if (rc) *rc = result;
    return output;
}

bool is_native_package_helper_invocation(int argc, char **argv)
{
    return argc > 1 && argv && std::string(argv[1]) == "--package-helper";
}

bool is_native_backend_invocation(int argc, char **argv)
{
    return argc > 1 && argv && std::string(argv[1]).rfind("--", 0) == 0;
}

int native_backend_main(int argc, char **argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    int rc = 0;
    std::string output = native_backend_capture(args, &rc);
    std::fwrite(output.data(), 1, output.size(), stdout);
    return rc;
}

int native_package_helper_main(int argc, char **argv)
{
    std::string action, value, desktop, transaction, pending;
    bool reinstall = false;
    bool force_overwrite = false;
    std::vector<std::string> execs;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--package-helper") action = next();
        else if (arg == "--package-value") value = next();
        else if (arg == "--package-reinstall") reinstall = true;
        else if (arg == "--package-force-overwrite") force_overwrite = true;
        else if (arg == "--package-desktop") desktop = next();
        else if (arg == "--package-exec") execs.push_back(next());
        else if (arg == "--package-transaction") transaction = next();
        else if (arg == "--package-pending-path") pending = next();
    }
    return package_helper(action, value, reinstall, force_overwrite, desktop, execs,
                          transaction, pending);
}

bool run_backend_process_mode(int argc, char **argv, int *exit_code)
{
    if (!is_native_package_helper_invocation(argc, argv) &&
        !is_native_backend_invocation(argc, argv))
        return false;
    const int result = is_native_package_helper_invocation(argc, argv)
        ? native_package_helper_main(argc, argv)
        : native_backend_main(argc, argv);
    if (exit_code) *exit_code = result;
    return true;
}

}  // namespace appstore
