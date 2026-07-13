#include "../main/ui/startup_network_flow.hpp"

#include <cassert>

int main()
{
    using namespace appstore;

    assert(startup_network_completion(false) == StartupNetworkCompletion::ENTER_HOME);
    assert(startup_network_completion(true) == StartupNetworkCompletion::STAY_FAILED);
    assert(startup_network_key_action(false, false) == StartupNetworkKeyAction::IGNORE);
    assert(startup_network_key_action(false, true) == StartupNetworkKeyAction::IGNORE);
    assert(startup_network_key_action(true, false) == StartupNetworkKeyAction::IGNORE);
    assert(startup_network_key_action(true, true) == StartupNetworkKeyAction::EXIT);
}
