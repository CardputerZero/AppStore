#include "main.h"
#include "appstore_process_entry.hpp"
#include "global_config.h"
#include "hv/hlog.h"

#ifdef CONFIG_V9_5_LV_USE_SDL
#include "hal/hal_paths.h"
#endif

#include <string>

namespace {

std::string executable_dir(const char *path)
{
    if (!path || !path[0]) return ".";
    const std::string value(path);
    const size_t slash = value.find_last_of('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? "/" : value.substr(0, slash);
}

} // namespace

int main(int argc, char **argv)
{
    hlog_set_handler(stderr_logger);
    int backend_exit_code = 0;
    if (appstore::run_backend_process_mode(argc, argv, &backend_exit_code))
        return backend_exit_code;
#ifdef CONFIG_V9_5_LV_USE_SDL
    const std::string directory = executable_dir(argv && argv[0] ? argv[0] : nullptr);
    hal_paths_init(directory.c_str());
#endif
    return run_appstore_app(argc, argv);
}
