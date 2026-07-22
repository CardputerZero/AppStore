#include "detached_worker_launcher.hpp"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>

int main()
{
    std::atomic<int> starts{0};
    std::atomic<int> finishes{0};
    std::mutex mutex;
    std::condition_variable done;
    bool ran = false;
    appstore::DetachedWorkerLauncher workers(
        [&]() { ++starts; },
        [&]() {
            ++finishes;
            done.notify_one();
        });
    assert(workers.start([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ran = true;
    }) == 0);
    std::unique_lock<std::mutex> lock(mutex);
    done.wait(lock, [&]() { return finishes.load() == 1; });
    assert(ran);
    assert(starts.load() == 1);
    std::cout << "detached worker launcher tests passed\n";
}
