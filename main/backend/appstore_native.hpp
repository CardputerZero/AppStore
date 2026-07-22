#pragma once

// Internal interface used by the AppStore backend bridge.

#include <string>
#include <vector>

namespace appstore {

std::string native_backend_capture(const std::vector<std::string> &args, int *rc);
void native_request_sync_cancel();
void native_request_package_cancel();

}  // namespace appstore
