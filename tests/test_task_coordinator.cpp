#include "appstore_task_coordinator.hpp"

#include <cassert>
#include <iostream>

int main()
{
    appstore_ui::AppStoreTaskCoordinator tasks;
    auto sync = tasks.sync().start({true});
    auto summary = tasks.summary().start({appstore::SortRule::New});
    auto registries = tasks.registry_refresh().start({"fallback"});
    assert(sync && summary && registries);
    assert(tasks.sync().running());

    appstore_ui::RegistryOpRequest operation_request;
    operation_request.kind = appstore_ui::RegistryOpKind::SetRegion;
    operation_request.region = "CN";
    assert(appstore_ui::AppStoreTaskCoordinator::registry_arguments(operation_request) ==
           std::vector<std::string>({"--set-region", "CN"}));
    auto operation = tasks.registry_operation().start(operation_request);
    auto plan = tasks.plan().start({"install", "app", appstore_ui::Screen::Detail});
    assert(operation && plan);

    tasks.cancel_catalog_work();
    assert(!tasks.sync().running());
    assert(!tasks.summary().running());
    assert(!tasks.registry_refresh().running());
    assert(tasks.registry_operation().running());
    assert(tasks.plan().running());

    std::cout << "task coordinator tests passed\n";
}
