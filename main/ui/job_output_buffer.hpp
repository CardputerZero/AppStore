#pragma once
#include <set>
#include <string>

namespace appstore {
constexpr size_t kJobOutputTailLimit = 256 * 1024;
constexpr size_t kErrorOutputLimit = 64 * 1024;
constexpr size_t kProtocolRecordLimit = 16 * 1024;

struct JobOutputBuffer {
    std::string tail;
    std::string last_package_job;
    std::string last_package_result;
    std::string errors;
    std::string pending;

    void clear() { tail.clear(); last_package_job.clear(); last_package_result.clear(); errors.clear(); pending.clear(); }
    void append_tail(const std::string &value) { tail += value; if (tail.size() > kJobOutputTailLimit) tail.erase(0, tail.size() - kJobOutputTailLimit); }

    void append(const char *data, size_t size)
    {
        tail.append(data, size);
        if (tail.size() > kJobOutputTailLimit) tail.erase(0, tail.size() - kJobOutputTailLimit);
        pending.append(data, size);
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, newline + 1);
            pending.erase(0, newline + 1);
            if (line.size() <= kProtocolRecordLimit && line.rfind("PACKAGE_JOB\t", 0) == 0) last_package_job = line;
            else if (line.size() <= kProtocolRecordLimit && line.rfind("PACKAGE_RESULT\t", 0) == 0) last_package_result = line;
            else if (line.rfind("ERROR\t", 0) == 0) errors += line;
        }
        if (errors.size() > kErrorOutputLimit) {
            size_t cut = errors.size() - kErrorOutputLimit;
            size_t next = errors.find('\n', cut);
            errors.erase(0, next == std::string::npos ? cut : next + 1);
        }
        if (pending.size() > kJobOutputTailLimit) pending.erase(0, pending.size() - kJobOutputTailLimit);
    }

    std::string snapshot() const
    {
        std::string out = last_package_job + last_package_result + errors;
        std::set<std::string> retained;
        if (!last_package_job.empty()) retained.insert(last_package_job);
        if (!last_package_result.empty()) retained.insert(last_package_result);
        size_t pos = 0;
        while (pos < errors.size()) { size_t n = errors.find('\n', pos); size_t e = n == std::string::npos ? errors.size() : n + 1; retained.insert(errors.substr(pos, e - pos)); pos = e; }
        pos = 0;
        while (pos < tail.size()) {
            size_t n = tail.find('\n', pos); size_t e = n == std::string::npos ? tail.size() : n + 1;
            std::string line = tail.substr(pos, e - pos);
            if (!retained.count(line)) out += line;
            pos = e;
        }
        return out;
    }

    size_t stored_size() const { return tail.size() + last_package_job.size() + last_package_result.size() + errors.size() + pending.size(); }
};
} // namespace appstore
