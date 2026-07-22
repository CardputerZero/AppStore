#pragma once

// UI-side shutdown state machine for package jobs.

namespace appstore_ui {

enum class ShutdownJobPhase { Idle, Prepare, Sudo, Finalize };
enum class ShutdownAction { KeepRunning, Ready, CancelPendingPrepare, CancelPrepare, CancelSudo };

inline ShutdownAction shutdown_action(bool quit_requested, bool job_running,
                                      bool job_pending_start, bool cancel_sent,
                                      ShutdownJobPhase phase, bool has_sudo_request)
{
    if (!quit_requested) return ShutdownAction::KeepRunning;
    if (!job_running && !job_pending_start) return ShutdownAction::Ready;
    if (cancel_sent) return ShutdownAction::KeepRunning;
    if (phase == ShutdownJobPhase::Prepare && job_pending_start)
        return ShutdownAction::CancelPendingPrepare;
    if (phase == ShutdownJobPhase::Prepare)
        return ShutdownAction::CancelPrepare;
    if (phase == ShutdownJobPhase::Sudo && has_sudo_request)
        return ShutdownAction::CancelSudo;
    return ShutdownAction::KeepRunning;
}

} // namespace appstore_ui
