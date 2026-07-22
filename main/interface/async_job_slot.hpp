#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace appstore {

template <typename Request>
struct AsyncJobTicket {
    uint64_t generation = 0;
    Request request{};
};

template <typename Request, typename Result>
class AsyncJobSlot
{
public:
    using Ticket = AsyncJobTicket<Request>;

    std::optional<Ticket> start(Request request)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) return std::nullopt;
        return start_locked(std::move(request));
    }

    std::optional<Ticket> start_or_defer(Request request)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            deferred_request_ = std::move(request);
            return std::nullopt;
        }
        return start_locked(std::move(request));
    }

    bool finish(uint64_t generation, Result result)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || generation != generation_) return false;
        result_ = std::move(result);
        done_ = true;
        return true;
    }

    bool take_result(Request &request, Result &result,
                     std::optional<Request> *deferred_request = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!done_) return false;
        request = std::move(request_);
        result = std::move(result_);
        if (deferred_request) *deferred_request = std::move(deferred_request_);
        deferred_request_.reset();
        request_ = Request{};
        result_ = Result{};
        running_ = false;
        done_ = false;
        return true;
    }

    void cancel()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        request_ = Request{};
        result_ = Result{};
        deferred_request_.reset();
        running_ = false;
        done_ = false;
    }

    void fail_start(uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ && generation == generation_) {
            request_ = Request{};
            result_ = Result{};
            running_ = false;
            done_ = false;
        }
    }

    bool running() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return running_;
    }

    std::optional<Request> active_request() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return std::nullopt;
        return request_;
    }

private:
    std::optional<Ticket> start_locked(Request request)
    {
        ++generation_;
        request_ = std::move(request);
        result_ = Result{};
        running_ = true;
        done_ = false;
        return Ticket{generation_, request_};
    }

    mutable std::mutex mutex_;
    Request request_{};
    Result result_{};
    std::optional<Request> deferred_request_;
    uint64_t generation_ = 0;
    bool running_ = false;
    bool done_ = false;
};

} // namespace appstore
