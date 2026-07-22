#pragma once

#include "appstore_image_renderer.hpp"
#include "appstore_session_state.hpp"
#include "appstore_task_coordinator.hpp"
#include "appstore_task_service.hpp"
#include "detached_worker_launcher.hpp"
#include "exit_controller.hpp"
#include "package_job_service.hpp"
#include "registry_persistence_service.hpp"
#include "shared_registry_store.hpp"
#include "system_status_state.hpp"

namespace appstore_ui {

class AppStoreRuntimeState
{
public:
    AppStoreRuntimeState();

    ExitController exit;
    AppStoreTaskCoordinator tasks;
    PackageJobService package_jobs;
    AppStoreSessionState session;
    SystemStatusState system_status;
    AppStoreImageRenderer images;
    appstore::SharedRegistryStore shared_registry;
    appstore::DetachedWorkerLauncher workers;
    AppStoreTaskService task_service;
    RegistryPersistenceService registry_persistence;
};

} // namespace appstore_ui
