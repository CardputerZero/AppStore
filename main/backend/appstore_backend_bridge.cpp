#include "appstore_backend_bridge.hpp"

#include "appstore_native.hpp"

#include <mutex>
#include <utility>
#include <vector>

#define def_hal_fun(signature, name) eventpp::CallbackList<signature> name;
#include "appstore_bridge_plan.h"
#undef def_hal_fun

void init_appstore_backend_bridge()
{
    static std::once_flag once;
    std::call_once(once, []() {
        appstore_signal_backend_api.append(
            [](std::list<std::string> arguments,
               std::function<void(int, std::string)> callback) {
                std::vector<std::string> values(arguments.begin(), arguments.end());
                int result = 0;
                std::string output = appstore::native_backend_capture(values, &result);
                if (callback)
                    callback(result, std::move(output));
            });
        appstore_signal_sync_cancel.append([](std::function<void(bool)> callback) {
            appstore::native_request_sync_cancel();
            if (callback)
                callback(true);
        });
        appstore_signal_package_cancel.append([](std::function<void(bool)> callback) {
            appstore::native_request_package_cancel();
            if (callback)
                callback(true);
        });
    });
}
