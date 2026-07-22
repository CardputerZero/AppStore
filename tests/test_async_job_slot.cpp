#include "../main/interface/async_job_slot.hpp"

#include <cassert>
#include <optional>
#include <string>

struct Request { std::string value; };
struct Result { int code = -1; std::string output; };

int main()
{
    appstore::AsyncJobSlot<Request, Result> slot;
    auto first = slot.start({"one"});
    assert(first && slot.running());
    assert(!slot.start({"two"}));
    assert(slot.finish(first->generation, {0, "done"}));

    Request request;
    Result result;
    assert(slot.take_result(request, result));
    assert(request.value == "one");
    assert(result.code == 0 && result.output == "done");

    auto active = slot.start_or_defer({"active"});
    assert(active);
    assert(slot.active_request() && slot.active_request()->value == "active");
    assert(!slot.start_or_defer({"deferred"}));
    assert(slot.finish(active->generation, {0, "active done"}));
    std::optional<Request> deferred;
    assert(slot.take_result(request, result, &deferred));
    assert(!slot.active_request());
    assert(deferred && deferred->value == "deferred");

    auto stale = slot.start({"stale"});
    assert(stale);
    slot.cancel();
    auto current = slot.start({"current"});
    assert(current);
    assert(!slot.finish(stale->generation, {1, "stale result"}));
    assert(slot.finish(current->generation, {0, "current result"}));
    assert(slot.take_result(request, result));
    assert(request.value == "current" && result.output == "current result");

    auto failed = slot.start({"failed"});
    assert(failed);
    slot.fail_start(failed->generation);
    assert(!slot.running());
    return 0;
}
