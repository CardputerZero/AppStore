#include "appstore_paths.hpp"

#include "appstore_business.hpp"
#if __has_include("global_config.h")
#include "global_config.h"
#endif

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

#ifndef CONFIG_V9_5_LV_FS_POSIX_PATH
#define CONFIG_V9_5_LV_FS_POSIX_PATH "/"
#endif

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

bool supported_media_file(const std::string &path)
{
    static constexpr unsigned char kPngSignature[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    if (path.empty()) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    unsigned char signature[sizeof(kPngSignature)] = {};
    file.read(reinterpret_cast<char *>(signature), sizeof(signature));
    return file.gcount() == static_cast<std::streamsize>(sizeof(signature)) &&
           std::equal(std::begin(signature), std::end(signature), std::begin(kPngSignature));
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
    if (!image.empty() && image[0] == '/') return supported_media_file(image) ? image : "";
    std::string root = parent_dir(app_dir);
    std::string candidate = root + "/" + image;
    return supported_media_file(candidate) ? candidate : "";
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
    return lvgl_posix_src_for_root(path, CONFIG_V9_5_LV_FS_POSIX_PATH);
}

std::string lvgl_posix_src_for_root(const std::string &path, const std::string &fs_root)
{
    if (path.empty()) return "";
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return path;
    }
    const std::filesystem::path source(path);
    const std::filesystem::path root(fs_root);
    if (source.is_absolute() && root.is_absolute()) {
        std::filesystem::path relative = source.lexically_normal().lexically_relative(root.lexically_normal());
        if (!relative.empty()) return "A:/" + relative.generic_string();
    }
    return "A:" + path;
}
