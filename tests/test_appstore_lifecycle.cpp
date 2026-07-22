#include "appstore_lifecycle.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using namespace appstore_ui;
    AppStoreSessionState session;
    PackageJobState job;
    ExitController exit;
    uint32_t now = 300;
    std::string calls;
    int finish_calls = 0;
    int prepare_cancels = 0;
    int sudo_cancels = 0;
    AppStoreLifecycle lifecycle(
        session, job, exit,
        {[&](int argc, char **) { assert(argc == 2); calls += 'C'; },
         [&]() { calls += 'R'; }, [&]() { calls += 'S'; },
         [&]() { calls += 'I'; }, [&]() { calls += 'V'; },
         [&]() { calls += 'G'; }, [&]() { calls += 'T'; },
         [&]() { calls += 'Q'; }, [&]() { calls += 'Y'; },
         [&]() { calls += 't'; }, [&]() { calls += 'O'; },
         [&]() { calls += 'B'; }, [&]() { calls += 'X'; },
         []() { return false; }, []() { return true; },
         [&]() { ++prepare_cancels; return true; },
         [&](uint64_t) { ++sudo_cancels; return 0; },
         [&](const std::string &, int) { ++finish_calls; },
         [&]() { return now; }});

    char arg0[] = "app";
    char *argv[] = {arg0, nullptr};
    lifecycle.initialize(2, argv);
    assert(calls == "RCSIVGV TQYV" || calls == "RCSIVGVTQYV");
    const std::string initialized_calls = calls;
    lifecycle.initialize(2, argv);
    assert(calls == initialized_calls);
    lifecycle.deinitialize();
    assert(calls.substr(calls.size() - 4) == "tOBX");
    lifecycle.deinitialize();

    exit.request();
    assert(lifecycle.should_quit());

    ExitController prepare_exit;
    prepare_exit.request();
    PackageJobState prepare_job;
    prepare_job.running = true;
    prepare_job.phase = PackageJobPhase::Prepare;
    AppStoreLifecycle prepare_lifecycle(
        session, prepare_job, prepare_exit,
        {{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
         []() { return false; }, []() { return true; },
         [&]() { ++prepare_cancels; return true; }, {}, {}, [&]() { return now; }});
    assert(!prepare_lifecycle.should_quit());
    assert(prepare_cancels == 1 && prepare_exit.job_cancel_sent());

    ExitController sudo_exit;
    sudo_exit.request();
    PackageJobState sudo_job;
    sudo_job.running = true;
    sudo_job.phase = PackageJobPhase::Sudo;
    sudo_job.sudo_request_id = 42;
    AppStoreLifecycle sudo_lifecycle(
        session, sudo_job, sudo_exit,
        {{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
         []() { return false; }, []() { return true; }, {},
         [&](uint64_t id) { assert(id == 42); ++sudo_cancels; return 0; },
         {}, [&]() { return now; }});
    assert(!sudo_lifecycle.should_quit());
    assert(sudo_cancels == 1 && sudo_exit.job_cancel_sent());
    assert(finish_calls == 0);

    std::cout << "appstore lifecycle tests passed\n";
}
