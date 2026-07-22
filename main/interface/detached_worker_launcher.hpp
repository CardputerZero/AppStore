#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <pthread.h>

namespace appstore {

class DetachedWorkerLauncher
{
public:
    using Callback = std::function<void()>;

    DetachedWorkerLauncher(Callback on_start, Callback on_finish)
        : on_start_(std::move(on_start)), on_finish_(std::move(on_finish))
    {
    }

    int start(Callback task)
    {
        auto payload = std::make_unique<Payload>(Payload{std::move(task), on_finish_});
        if (on_start_) on_start_();
        pthread_t thread;
        const int rc = pthread_create(&thread, nullptr, &DetachedWorkerLauncher::run,
                                      payload.get());
        if (rc != 0) {
            if (on_finish_) on_finish_();
            return rc;
        }
        payload.release();
        pthread_detach(thread);
        return 0;
    }

private:
    struct Payload {
        Callback task;
        Callback on_finish;
    };

    static void *run(void *arg)
    {
        std::unique_ptr<Payload> payload(static_cast<Payload *>(arg));
        payload->task();
        if (payload->on_finish) payload->on_finish();
        return nullptr;
    }

    Callback on_start_;
    Callback on_finish_;
};

} // namespace appstore
