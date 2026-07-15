#pragma once

#include <string>
#include <vector>

#include "appstore_business.hpp"

std::string dirname_of(const char *argv0);
std::string parent_dir(const std::string &path);
bool file_exists(const std::string &path);
bool supported_media_file(const std::string &path);
std::string resolve_script_path(const std::string &binary_dir);
std::string resolve_media_path(const std::string &app_dir, std::string image);
std::string icon_file_path(const std::string &app_dir, const appstore::StoreApp &app);
std::vector<std::string> detail_screenshot_paths(const std::string &app_dir, const appstore::StoreApp &app);
std::string lvgl_posix_src(const std::string &path);
std::string lvgl_posix_src_for_root(const std::string &path, const std::string &fs_root);
