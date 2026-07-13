#pragma once

namespace appstore {

enum class StartupNetworkCompletion { ENTER_HOME, STAY_FAILED };
enum class StartupNetworkKeyAction { IGNORE, EXIT };

inline StartupNetworkCompletion startup_network_completion(bool failed)
{
    return failed ? StartupNetworkCompletion::STAY_FAILED
                  : StartupNetworkCompletion::ENTER_HOME;
}

inline StartupNetworkKeyAction startup_network_key_action(bool failed, bool ok_pressed)
{
    return failed && ok_pressed ? StartupNetworkKeyAction::EXIT
                                : StartupNetworkKeyAction::IGNORE;
}

} // namespace appstore
