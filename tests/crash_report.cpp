#include <cstring>
#include <cstdint>
#include "../base/utils/crash_log.h"
#include <fstream>
#include <iterator>
#include <cassert>

int main() {
    // Exercise the overflow-report branch with a valid captured context. This
    // verifies report contents, not recovery from an actual exhausted stack.
    CreateDirectoryA("build\\crash-report-test", nullptr);
    strcpy_s(CrashLog::g_LogDir, "build\\crash-report-test");
    strcpy_s(CrashLog::g_Stamp, "overflow");
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_STACK_OVERFLOW;
    record.ExceptionAddress = reinterpret_cast<void*>(&main);
    EXCEPTION_POINTERS pointers{&record, &context};
    CrashLog::Mark("test: overflow breadcrumb");
    CrashLog::WriteReport(&pointers, "STACK_OVERFLOW");
    std::ifstream file("build\\crash-report-test\\crash_overflow_0.log");
    const std::string report{std::istreambuf_iterator<char>(file), {}};
    assert(report.find("stack limit=") != std::string::npos);
    assert(report.find("test: overflow breadcrumb") != std::string::npos);
    assert(report.find("symbol walk and minidump skipped") != std::string::npos);
}
