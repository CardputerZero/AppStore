#include "../main/ui/appstore_paths.hpp"

#include <cassert>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace appstore {

std::string first_csv(std::string value)
{
    const size_t comma = value.find(',');
    if (comma != std::string::npos) value.resize(comma);
    return value;
}

std::vector<std::string> split_csv_paths(const std::string &value)
{
    std::vector<std::string> out;
    size_t begin = 0;
    while (begin <= value.size()) {
        size_t end = value.find(',', begin);
        std::string item = value.substr(begin, end - begin);
        if (!item.empty()) out.push_back(item);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return out;
}

} // namespace appstore

static void write_bytes(const std::string &path, const unsigned char *data, size_t size)
{
    int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, data, size) == static_cast<ssize_t>(size));
    close(fd);
}

int main()
{
    assert(lvgl_posix_src_for_root("A:/share/icon.png", "/usr/share/APPLaunch/") ==
           "A:/share/icon.png");
    assert(lvgl_posix_src_for_root("/usr/share/APPLaunch/share/icon.png", "/usr/share/APPLaunch/") ==
           "A:/share/icon.png");
    assert(lvgl_posix_src_for_root("/var/cache/APPLaunch/icon.png", "/usr/share/APPLaunch/") ==
           "A:/../../../var/cache/APPLaunch/icon.png");
    assert(lvgl_posix_src_for_root("/home/pi/.cache/icon.png", "/usr/share/APPLaunch/") ==
           "A:/../../../home/pi/.cache/icon.png");
    assert(lvgl_posix_src_for_root("/home/pi/../pi/.cache/icon.png", "/usr/share/APPLaunch/") ==
           "A:/../../../home/pi/.cache/icon.png");
    assert(lvgl_posix_src_for_root("share/icon.png", "/usr/share/APPLaunch/") ==
           "A:share/icon.png");

    char dir_template[] = "/tmp/appstore-paths-XXXXXX";
    char *dir = mkdtemp(dir_template);
    assert(dir != nullptr);
    const std::string root(dir);
    const std::string app_dir = root + "/bin";
    assert(mkdir(app_dir.c_str(), 0700) == 0);

    static constexpr unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00,
    };
    static constexpr unsigned char truncated[] = {0x89, 0x50, 0x4e};
    static constexpr unsigned char html[] = {'<', 'h', 't', 'm', 'l', '>'};
    const std::string valid = root + "/valid.png";
    const std::string short_png = root + "/short.png";
    const std::string html_file = root + "/error.png";
    write_bytes(valid, png, sizeof(png));
    write_bytes(short_png, truncated, sizeof(truncated));
    write_bytes(html_file, html, sizeof(html));

    assert(supported_media_file(valid));
    assert(!supported_media_file(short_png));
    assert(!supported_media_file(html_file));
    assert(!supported_media_file(root));
    assert(!supported_media_file(root + "/missing.png"));
    assert(resolve_media_path(app_dir, valid) == valid);
    assert(resolve_media_path(app_dir, "file://" + valid) == valid);
    assert(resolve_media_path(app_dir, "valid.png") == valid);
    assert(resolve_media_path(app_dir, short_png).empty());

    appstore::StoreApp app;
    app.images = valid + "," + short_png + "," + html_file + "," + valid;
    assert(icon_file_path(app_dir, app) == valid);
    const auto screenshots = detail_screenshot_paths(app_dir, app);
    assert(screenshots.size() == 1 && screenshots[0] == valid);

    unlink(valid.c_str());
    unlink(short_png.c_str());
    unlink(html_file.c_str());
    rmdir(app_dir.c_str());
    rmdir(root.c_str());
}
