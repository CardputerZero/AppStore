#pragma once

#include "eventpp/callbacklist.h"

#include <functional>
#include <list>
#include <string>

#define def_hal_fun(signature, name) extern eventpp::CallbackList<signature> name;
#include "appstore_bridge_plan.h"
#undef def_hal_fun

// Registers the native backend listeners exactly once.
void init_appstore_backend_bridge();
