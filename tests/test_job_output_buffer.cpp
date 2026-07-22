#include "../main/interface/job_output_buffer.hpp"
#include <cassert>
#include <cstring>
int main()
{
    appstore::JobOutputBuffer buffer;
    const char *a = "noise\nPACKAGE_RES", *b = "ULT\tinstall\tapp\tpkg\t1\n";
    buffer.append(a, std::strlen(a)); buffer.append(b, std::strlen(b));
    const std::string result = "PACKAGE_RESULT\tinstall\tapp\tpkg\t1\n";
    assert(buffer.last_package_result == result);
    assert(buffer.snapshot().find(result) == buffer.snapshot().rfind(result));
    const std::string older = "PACKAGE_JOB\told\tx\tx\tx\tx\tx\n";
    buffer.append(older.data(), older.size());
    const std::string newer = "PACKAGE_JOB\tnew\tx\tx\tx\tx\tx\n";
    buffer.append(newer.data(), newer.size());
    assert(buffer.last_package_job == newer);
    const std::string error = "ERROR\tfailure detail padding padding padding\n";
    for (size_t n = 0; n < 300 * 1024 / error.size(); ++n) buffer.append(error.data(), error.size());
    std::string snap = buffer.snapshot();
    assert(snap.find(result) != std::string::npos);
    assert(buffer.tail.size() <= appstore::kJobOutputTailLimit);
    assert(buffer.errors.size() <= appstore::kErrorOutputLimit);
    assert(buffer.pending.size() <= appstore::kJobOutputTailLimit);
    assert(buffer.stored_size() <= 2 * appstore::kJobOutputTailLimit + appstore::kErrorOutputLimit + 2 * appstore::kProtocolRecordLimit);

    appstore::JobOutputBuffer backend;
    std::string blob = result + std::string(appstore::kJobOutputTailLimit + 8192, 'z');
    backend.append(blob.data(), blob.size());
    assert(backend.snapshot().find(result) != std::string::npos);
}
