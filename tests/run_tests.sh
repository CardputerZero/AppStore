#!/bin/sh
set -eu
build_dir="${TMPDIR:-/tmp}/appstore-tests"
ext_components_path="${CARDPUTERZERO_EXT_COMPONENTS_PATH:-$(dirname "$0")/../../../ext_components}"
mkdir -p "$build_dir"
"$(dirname "$0")/test_backend_boundaries.sh"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/include" -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_paths.cpp" \
    "$(dirname "$0")/../main/ui/appstore_paths.cpp" \
    -o "$build_dir/test_appstore_paths"
"$build_dir/test_appstore_paths"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_catalog_state.cpp" \
    "$(dirname "$0")/../main/ui/catalog_state.cpp" \
    -o "$build_dir/test_catalog_state"
"$build_dir/test_catalog_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_catalog_controller.cpp" \
    "$(dirname "$0")/../main/ui/catalog_controller.cpp" \
    "$(dirname "$0")/../main/ui/catalog_state.cpp" \
    "$(dirname "$0")/../main/ui/search_state.cpp" \
    "$(dirname "$0")/../main/ui/share_code_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_text.cpp" \
    -o "$build_dir/test_catalog_controller"
"$build_dir/test_catalog_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    "$(dirname "$0")/test_status_message_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    -o "$build_dir/test_status_message_state"
"$build_dir/test_status_message_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    "$(dirname "$0")/test_async_job_slot.cpp" \
    -o "$build_dir/test_async_job_slot"
"$build_dir/test_async_job_slot"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" \
    "$(dirname "$0")/test_shared_registry_store.cpp" \
    "$(dirname "$0")/../main/interface/shared_registry_store.cpp" \
    -o "$build_dir/test_shared_registry_store"
"$build_dir/test_shared_registry_store"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_registry_persistence_service.cpp" \
    "$(dirname "$0")/../main/ui/registry_persistence_service.cpp" \
    "$(dirname "$0")/../main/ui/registry_ui_state.cpp" \
    "$(dirname "$0")/../main/interface/shared_registry_store.cpp" \
    -o "$build_dir/test_registry_persistence_service"
"$build_dir/test_registry_persistence_service"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_package_job_state.cpp" \
    "$(dirname "$0")/../main/ui/package_job_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_package_job_state"
"$build_dir/test_package_job_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_search_state.cpp" \
    "$(dirname "$0")/../main/ui/search_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_text.cpp" \
    -o "$build_dir/test_search_state"
"$build_dir/test_search_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_registry_ui_state.cpp" \
    "$(dirname "$0")/../main/ui/registry_ui_state.cpp" \
    -o "$build_dir/test_registry_ui_state"
"$build_dir/test_registry_ui_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_registry_controller.cpp" \
    "$(dirname "$0")/../main/ui/registry_controller.cpp" \
    "$(dirname "$0")/../main/ui/appstore_task_service.cpp" \
    "$(dirname "$0")/../main/ui/registry_ui_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_registry_controller"
"$build_dir/test_registry_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_confirmation_state.cpp" \
    "$(dirname "$0")/../main/ui/confirmation_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_confirmation_state"
"$build_dir/test_confirmation_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_detail_media_state.cpp" \
    "$(dirname "$0")/../main/ui/detail_media_state.cpp" \
    -o "$build_dir/test_detail_media_state"
"$build_dir/test_detail_media_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_detail_action_controller.cpp" \
    "$(dirname "$0")/../main/ui/detail_action_controller.cpp" \
    "$(dirname "$0")/../main/ui/catalog_state.cpp" \
    "$(dirname "$0")/../main/ui/confirmation_state.cpp" \
    "$(dirname "$0")/../main/ui/detail_media_state.cpp" \
    "$(dirname "$0")/../main/ui/package_job_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_detail_action_controller"
"$build_dir/test_detail_action_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_share_code_state.cpp" \
    "$(dirname "$0")/../main/ui/share_code_state.cpp" \
    -o "$build_dir/test_share_code_state"
"$build_dir/test_share_code_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_exit_controller.cpp" \
    "$(dirname "$0")/../main/ui/exit_controller.cpp" \
    -o "$build_dir/test_exit_controller"
"$build_dir/test_exit_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_lifecycle.cpp" \
    "$(dirname "$0")/../main/ui/appstore_lifecycle.cpp" \
    "$(dirname "$0")/../main/ui/exit_controller.cpp" \
    "$(dirname "$0")/../main/ui/package_job_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_appstore_lifecycle"
"$build_dir/test_appstore_lifecycle"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_task_coordinator.cpp" \
    -o "$build_dir/test_task_coordinator"
"$build_dir/test_task_coordinator"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" \
    "$(dirname "$0")/test_detached_worker_launcher.cpp" \
    -o "$build_dir/test_detached_worker_launcher"
"$build_dir/test_detached_worker_launcher"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_package_job_service.cpp" \
    "$(dirname "$0")/../main/ui/package_job_service.cpp" \
    "$(dirname "$0")/../main/ui/package_job_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_package_job_service"
"$build_dir/test_package_job_service"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_package_operation_controller.cpp" \
    "$(dirname "$0")/../main/ui/package_operation_controller.cpp" \
    "$(dirname "$0")/../main/ui/package_job_service.cpp" \
    "$(dirname "$0")/../main/ui/package_job_state.cpp" \
    "$(dirname "$0")/../main/ui/confirmation_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_package_operation_controller"
"$build_dir/test_package_operation_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_task_service.cpp" \
    "$(dirname "$0")/../main/ui/appstore_task_service.cpp" \
    -o "$build_dir/test_appstore_task_service"
"$build_dir/test_appstore_task_service"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_request_coordinator.cpp" \
    "$(dirname "$0")/../main/ui/appstore_request_coordinator.cpp" \
    "$(dirname "$0")/../main/ui/appstore_task_service.cpp" \
    "$(dirname "$0")/../main/ui/catalog_controller.cpp" \
    "$(dirname "$0")/../main/ui/catalog_state.cpp" \
    "$(dirname "$0")/../main/ui/confirmation_state.cpp" \
    "$(dirname "$0")/../main/ui/detail_action_controller.cpp" \
    "$(dirname "$0")/../main/ui/detail_media_state.cpp" \
    "$(dirname "$0")/../main/ui/package_job_state.cpp" \
    "$(dirname "$0")/../main/ui/search_state.cpp" \
    "$(dirname "$0")/../main/ui/share_code_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_appstore_request_coordinator"
"$build_dir/test_appstore_request_coordinator"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_sync_controller.cpp" \
    "$(dirname "$0")/../main/ui/sync_controller.cpp" \
    "$(dirname "$0")/../main/ui/appstore_task_service.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_sync_controller"
"$build_dir/test_sync_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_session_state.cpp" \
    -o "$build_dir/test_appstore_session_state"
"$build_dir/test_appstore_session_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_view_model_factory.cpp" \
    "$(dirname "$0")/../main/ui/appstore_view_model_factory.cpp" \
    "$(dirname "$0")/../main/ui/catalog_state.cpp" \
    "$(dirname "$0")/../main/ui/detail_media_state.cpp" \
    "$(dirname "$0")/../main/ui/registry_ui_state.cpp" \
    "$(dirname "$0")/../main/ui/search_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    -o "$build_dir/test_appstore_view_model_factory"
"$build_dir/test_appstore_view_model_factory"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -pthread \
    -I"$(dirname "$0")/../main/interface" -I"$(dirname "$0")/../main/ui" \
    -I"$ext_components_path/cp0_lvgl/include" \
    "$(dirname "$0")/test_appstore_input_controller.cpp" \
    "$(dirname "$0")/../main/ui/appstore_input_controller.cpp" \
    "$(dirname "$0")/../main/ui/catalog_controller.cpp" \
    "$(dirname "$0")/../main/ui/catalog_state.cpp" \
    "$(dirname "$0")/../main/ui/confirmation_state.cpp" \
    "$(dirname "$0")/../main/ui/detail_action_controller.cpp" \
    "$(dirname "$0")/../main/ui/detail_media_state.cpp" \
    "$(dirname "$0")/../main/ui/exit_controller.cpp" \
    "$(dirname "$0")/../main/ui/package_job_state.cpp" \
    "$(dirname "$0")/../main/ui/registry_controller.cpp" \
    "$(dirname "$0")/../main/ui/registry_ui_state.cpp" \
    "$(dirname "$0")/../main/ui/search_state.cpp" \
    "$(dirname "$0")/../main/ui/share_code_state.cpp" \
    "$(dirname "$0")/../main/ui/status_message_state.cpp" \
    "$(dirname "$0")/../main/ui/sync_controller.cpp" \
    "$(dirname "$0")/../main/ui/appstore_task_service.cpp" \
    "$(dirname "$0")/../main/interface/appstore_protocol.cpp" \
    -o "$build_dir/test_appstore_input_controller"
"$build_dir/test_appstore_input_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/ui" \
    -I"$ext_components_path/cp0_lvgl/include" \
    "$(dirname "$0")/test_system_status_state.cpp" \
    -o "$build_dir/test_system_status_state"
"$build_dir/test_system_status_state"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/ui" \
    -I"$ext_components_path/cp0_lvgl/include" \
    "$(dirname "$0")/test_system_status_controller.cpp" \
    "$(dirname "$0")/../main/ui/system_status_controller.cpp" \
    -o "$build_dir/test_system_status_controller"
"$build_dir/test_system_status_controller"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_refresh_coordinator.cpp" \
    "$(dirname "$0")/../main/ui/appstore_refresh_coordinator.cpp" \
    -o "$build_dir/test_appstore_refresh_coordinator"
"$build_dir/test_appstore_refresh_coordinator"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_job_output_buffer.cpp" -o "$build_dir/test_job_output_buffer"
"$build_dir/test_job_output_buffer"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_startup_network_flow.cpp" -o "$build_dir/test_startup_network_flow"
"$build_dir/test_startup_network_flow"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_job_shutdown_flow.cpp" -o "$build_dir/test_job_shutdown_flow"
"$build_dir/test_job_shutdown_flow"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_low_battery_flow.cpp" -o "$build_dir/test_low_battery_flow"
"$build_dir/test_low_battery_flow"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -isystem "$(dirname "$0")/../../../SDK/components/utilities/include" \
    "$(dirname "$0")/test_native_backend.cpp" -o "$build_dir/test_native_backend"
native_binary="$(dirname "$0")/../dist/M5CardputerZero-AppStore"
if file "$native_binary" | grep -q 'ELF' && [ "$(uname -s)" != "Linux" ]; then
    echo "native backend runtime tests skipped: Linux target binary on $(uname -s)"
else
    "$build_dir/test_native_backend" "$native_binary"
    "$(dirname "$0")/test_native_backend.sh"
    "$(dirname "$0")/test_package_repair_helper.sh" "$native_binary"
fi
