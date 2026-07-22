#include "../main/interface/job_shutdown_flow.hpp"
#include <cassert>

using appstore_ui::ShutdownAction;
using appstore_ui::ShutdownJobPhase;
using appstore_ui::shutdown_action;

int main()
{
    assert(shutdown_action(false, false, false, false, ShutdownJobPhase::Idle, false) ==
           ShutdownAction::KeepRunning);
    assert(shutdown_action(true, false, false, false, ShutdownJobPhase::Idle, false) ==
           ShutdownAction::Ready);
    assert(shutdown_action(true, true, true, false, ShutdownJobPhase::Prepare, false) ==
           ShutdownAction::CancelPendingPrepare);
    assert(shutdown_action(true, true, false, false, ShutdownJobPhase::Prepare, false) ==
           ShutdownAction::CancelPrepare);
    assert(shutdown_action(true, true, false, false, ShutdownJobPhase::Sudo, true) ==
           ShutdownAction::CancelSudo);
    assert(shutdown_action(true, true, false, false, ShutdownJobPhase::Sudo, false) ==
           ShutdownAction::KeepRunning);
    assert(shutdown_action(true, true, false, true, ShutdownJobPhase::Sudo, true) ==
           ShutdownAction::KeepRunning);
    assert(shutdown_action(true, true, true, false, ShutdownJobPhase::Finalize, false) ==
           ShutdownAction::KeepRunning);
}
