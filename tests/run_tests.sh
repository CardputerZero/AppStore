#!/bin/sh
set -eu
build_dir="${TMPDIR:-/tmp}/appstore-tests"
mkdir -p "$build_dir"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror \
    -I"$(dirname "$0")/../main/include" -I"$(dirname "$0")/../main/ui" \
    "$(dirname "$0")/test_appstore_paths.cpp" \
    "$(dirname "$0")/../main/ui/appstore_paths.cpp" \
    -o "$build_dir/test_appstore_paths"
"$build_dir/test_appstore_paths"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_job_output_buffer.cpp" -o "$build_dir/test_job_output_buffer"
"$build_dir/test_job_output_buffer"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_startup_network_flow.cpp" -o "$build_dir/test_startup_network_flow"
"$build_dir/test_startup_network_flow"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_job_shutdown_flow.cpp" -o "$build_dir/test_job_shutdown_flow"
"$build_dir/test_job_shutdown_flow"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror "$(dirname "$0")/test_low_battery_flow.cpp" -o "$build_dir/test_low_battery_flow"
"$build_dir/test_low_battery_flow"
python3 -m unittest discover -s "$(dirname "$0")" -p 'test_*.py'
