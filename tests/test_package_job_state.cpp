#include "../main/ui/package_job_state.hpp"

#include <cassert>
#include <algorithm>

int main()
{
    appstore_ui::PackageJobState state;
    state.begin("install", "app.id", "App", 42);
    assert(state.running && state.pending_start);
    assert(state.phase == appstore_ui::PackageJobPhase::Prepare);
    assert(state.action == "install" && state.app_id == "app.id");

    state.complete_backend("PACKAGE_RESULT\tOK\n", 0, true);
    auto result = state.result_snapshot();
    assert(result.done && result.rc == 0);
    assert(result.output.find("PACKAGE_RESULT") != std::string::npos);

    assert(state.parse_prepare_output(
        "PACKAGE_JOB\tinstall\tpkg.deb\t1\tapp.desktop\ttx-1\t/tmp/pending\t/bin/app\n"));
    const auto arguments = state.sudo_arguments("appstore");
    assert(arguments.size() == 14);
    assert(arguments[0] == "appstore" && arguments[1] == "--package-helper");
    assert(arguments[2] == "install" && arguments[4] == "pkg.deb");
    assert(arguments.back() == "/bin/app");

    state.clear_backend_result(false);
    result = state.result_snapshot();
    assert(!result.done && result.rc == -1);
    assert(result.output.find("PACKAGE_RESULT") != std::string::npos);

    state.begin("install", "app.id", "App", 42, true);
    assert(state.parse_prepare_output(
        "PACKAGE_JOB\tinstall\tpkg.deb\t0\tapp.desktop\ttx-2\t/tmp/pending\n"));
    const auto forced_arguments = state.sudo_arguments("appstore");
    assert(state.force_overwrite);
    assert(std::find(forced_arguments.begin(), forced_arguments.end(),
                     "--package-force-overwrite") != forced_arguments.end());

    state.reset();
    assert(!state.running && !state.pending_start);
    assert(state.phase == appstore_ui::PackageJobPhase::Idle);
    assert(state.result_snapshot().output.empty());
    return 0;
}
