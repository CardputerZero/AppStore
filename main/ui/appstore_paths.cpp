#include "appstore_paths.hpp"

#include "appstore_business.hpp"

#include <cstring>
#include <cstdlib>
#include <fstream>
#include <cctype>
#include <unistd.h>
#include <vector>

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

bool file_exists(const std::string &path)
{
    if (path.empty()) return false;
    std::ifstream file(path);
    return file.good();
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

std::string resolve_media_path(const std::string &app_dir, std::string image)
{
    if (image.empty()) return "";
    if (image.rfind("file://", 0) == 0) image = image.substr(7);
    if (!image.empty() && image[0] == '/') return file_exists(image) ? image : "";
    std::string root = parent_dir(app_dir);
    std::string candidate = root + "/" + image;
    return file_exists(candidate) ? candidate : "";
}

std::string icon_file_path(const std::string &app_dir, const appstore::StoreApp &app)
{
    return resolve_media_path(app_dir, appstore::first_csv(app.images));
}

std::vector<std::string> detail_screenshot_paths(const std::string &app_dir, const appstore::StoreApp &app)
{
    std::vector<std::string> images = appstore::split_csv_paths(app.images);
    std::vector<std::string> out;
    for (size_t i = 1; i < images.size(); ++i) {
        std::string path = resolve_media_path(app_dir, images[i]);
        if (!path.empty()) out.push_back(path);
    }
    return out;
}

std::string lvgl_posix_src(const std::string &path)
{
    if (path.empty()) return "";
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return path;
    }
    static constexpr const char *kAppRoot = "/usr/share/APPLaunch/";
    if (path.rfind(kAppRoot, 0) == 0) {
        return "A:/" + path.substr(std::strlen(kAppRoot));
    }
    return "A:" + path;
}
