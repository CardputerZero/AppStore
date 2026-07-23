#include "appstore_runtime_state.hpp"

#include "appstore_client.hpp"
#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"

#include <cstdio>
#include <utility>

void cp0_zmq_log(const char *topic, const char *message);

namespace appstore_ui {
namespace {

void runtime_log(const std::string &message)
{
    std::fprintf(stderr, "[Store TRACE] %s\n", message.c_str());
    cp0_zmq_log("appstore", message.c_str());
}

} // namespace

AppStoreRuntimeState::AppStoreRuntimeState()
    : package_jobs([](const std::vector<std::string> &args, int *rc) {
          return appstore::backend_capture(args, rc);
      }),
      shared_registry(
          [](const std::string &key, const std::string &fallback) {
              std::string value = fallback;
              cp0_signal_config_api(
                  {"GetStr", key, fallback},
                  [&](int code, std::string data) {
                      if (code == 0) value = std::move(data);
                  });
              return value;
          },
          [](const std::string &key, const std::string &value) {
              cp0_signal_config_api({"SetStr", key, value}, nullptr);
          },
          []() { cp0_signal_config_api({"Save"}, nullptr); }),
      workers([this]() { exit.worker_started(); },
              [this]() { exit.worker_finished(); }),
      task_service(tasks, workers,
                   [](const std::vector<std::string> &args, int *rc) {
                       return appstore::backend_capture(args, rc);
                   }),
      registry_persistence(
          session, shared_registry,
          {[]() { return appstore::load_registry_config(); },
           [](const appstore::RegistryConfig &config) {
               return appstore::replace_registry_config(config);
           },
           [](const std::string &message) { runtime_log(message); }})
{
}

} // namespace appstore_ui
