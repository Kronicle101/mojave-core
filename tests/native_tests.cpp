#include "io_cache.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s (error=%lu)\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
namespace {
std::wstring path;
std::vector<BYTE> content(2 * 1024 * 1024 + 117);
HANDLE Open() { return fnv::io::OpenW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); }
void Compare(DWORD ahead) {
    fnv::Config config;
    config.cacheMiB = 1;
    config.readAheadKiB = ahead;
    config.minAvailableMiB = 0;
    config.minVirtualMiB = 0;
    config.maxMemoryLoad = 100;
    fnv::io::Configure(config);
    HANDLE cached = Open();
    HANDLE native = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(cached != INVALID_HANDLE_VALUE && native != INVALID_HANDLE_VALUE);
    std::mt19937 random(451);
    std::vector<BYTE> actual(400000), expected(400000);
    DWORD requests[] = {0, 1, 7, 4096, 65536, 262144, 400000};
    for (int i = 0; i < 1200; ++i) {
        LARGE_INTEGER offset{};
        offset.QuadPart = i < 10 ? static_cast<LONGLONG>(content.size() - i) : random() % (content.size() + 4096);
        const DWORD count = requests[random() % std::size(requests)];
        CHECK(fnv::io::SeekEx(cached, offset, nullptr, FILE_BEGIN));
        CHECK(SetFilePointerEx(native, offset, nullptr, FILE_BEGIN));
        DWORD got{}, reference{};
        const BOOL a = fnv::io::Read(cached, actual.data(), count, &got, nullptr);
        const BOOL b = ReadFile(native, expected.data(), count, &reference, nullptr);
        CHECK(a == b && got == reference && std::equal(actual.begin(), actual.begin() + got, expected.begin()));
        LARGE_INTEGER zero{}, pa{}, pb{};
        CHECK(fnv::io::SeekEx(cached, zero, &pa, FILE_CURRENT));
        CHECK(SetFilePointerEx(native, zero, &pb, FILE_CURRENT));
        CHECK(pa.QuadPart == pb.QuadPart);
        // Repeat: this should exercise cache hits, including short reads at EOF.
        CHECK(fnv::io::SeekEx(cached, offset, nullptr, FILE_BEGIN));
        CHECK(fnv::io::Read(cached, actual.data(), count, &got, nullptr));
        CHECK(got == reference && std::equal(actual.begin(), actual.begin() + got, expected.begin()));
        CHECK(fnv::io::Snapshot().allocatedBytes <= 1024 * 1024);
    }
    CHECK(fnv::io::Snapshot().hits > 100);
    CHECK(fnv::io::Snapshot().evictions > 0);
    // Negative seeks return the same Win32 failure and leave the pointer unchanged.
    LARGE_INTEGER negative{}; negative.QuadPart = -1;
    CHECK(!fnv::io::SeekEx(cached, negative, nullptr, FILE_BEGIN));
    CHECK(GetLastError() == ERROR_NEGATIVE_SEEK);
    CHECK(fnv::io::Close(cached));
    CHECK(CloseHandle(native));
    CHECK(fnv::io::Snapshot().allocatedBytes == 0);
    CHECK(fnv::io::Snapshot().trackedHandles == 0);
    std::printf("PASS 2400 reads matching Win32, readahead=%lu KiB; bounded LRU, EOF and pointer contract\n", ahead);
}
}
int main() {
    wchar_t temporary[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH, temporary));
    path = std::wstring(temporary) + L"FNVStabilizer-test-" + std::to_wstring(GetCurrentProcessId()) + L".bsa";
    for (size_t i = 0; i < content.size(); ++i) content[i] = static_cast<BYTE>((i * 37 + (i >> 8)) & 255);
    HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    CHECK(writer != INVALID_HANDLE_VALUE);
    DWORD written{};
    CHECK(WriteFile(writer, content.data(), static_cast<DWORD>(content.size()), &written, nullptr));
    CHECK(written == content.size() && CloseHandle(writer));
    Compare(0);
    Compare(256);
    fnv::Config config;
    config.minAvailableMiB = 0; config.minVirtualMiB = 0; config.maxMemoryLoad = 100;
    fnv::io::Configure(config);
    // Files open for writing, shared with writers, and async/non-buffered files opt out.
    HANDLE shared = fnv::io::OpenW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(shared != INVALID_HANDLE_VALUE && fnv::io::Snapshot().trackedHandles == 0);
    CHECK(fnv::io::Close(shared));
    HANDLE async = fnv::io::OpenW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    CHECK(async != INVALID_HANDLE_VALUE && fnv::io::Snapshot().trackedHandles == 0);
    OVERLAPPED operation{};
    operation.Offset = 512;
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BYTE asyncData[128]{};
    DWORD got{};
    const BOOL asyncResult = fnv::io::Read(async, asyncData, sizeof(asyncData), &got, &operation);
    CHECK(asyncResult || GetLastError() == ERROR_IO_PENDING);
    CHECK(GetOverlappedResult(async, &operation, &got, TRUE));
    CHECK(got == sizeof(asyncData) && memcmp(asyncData, content.data() + 512, got) == 0);
    CHECK(fnv::io::Close(async));
    CHECK(CloseHandle(operation.hEvent));
    // Independent archive handles in four loader threads.
    std::atomic<bool> valid{true};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back([&] {
        HANDLE file = Open();
        if (file == INVALID_HANDLE_VALUE) { valid = false; return; }
        for (DWORD i = 0; i < 100; ++i) {
            BYTE buffer[4096]; DWORD count{};
            if (!fnv::io::Read(file, buffer, sizeof(buffer), &count, nullptr) || count != sizeof(buffer) ||
                memcmp(buffer, content.data() + size_t(i) * sizeof(buffer), count) != 0) valid = false;
        }
        if (!fnv::io::Close(file)) valid = false;
    });
    for (auto& thread : threads) thread.join();
    CHECK(valid && fnv::io::Snapshot().allocatedBytes == 0);
    HANDLE file = Open();
    CHECK(file != INVALID_HANDLE_VALUE);
    LONG high{};
    CHECK(fnv::io::Seek(file, 123, &high, FILE_BEGIN) == 123 && high == 0);
    BYTE bytes[4096]{};
    CHECK(fnv::io::Read(file, bytes, sizeof(bytes), &got, nullptr));
    CHECK(memcmp(bytes, content.data() + 123, got) == 0);
    CHECK(fnv::io::Snapshot().allocatedBytes > 0);
    fnv::io::Trim();
    CHECK(fnv::io::Snapshot().allocatedBytes == 0);
    CHECK(fnv::io::Close(file));
    config.minAvailableMiB = MAXDWORD;
    fnv::io::Configure(config);
    file = Open();
    CHECK(fnv::io::Read(file, bytes, sizeof(bytes), &got, nullptr));
    CHECK(fnv::io::Read(file, bytes, sizeof(bytes), &got, nullptr));
    CHECK(fnv::io::Snapshot().allocatedBytes == 0);
    CHECK(fnv::io::Close(file));
    MEMORYSTATUSEX memory{};
    memory.ullAvailPhys = uint64_t(2048) << 20;
    memory.ullAvailVirtual = uint64_t(512) << 20;
    memory.dwMemoryLoad = 50;
    fnv::Config thresholds;
    CHECK(!fnv::UnderPressure(memory, thresholds));
    memory.dwMemoryLoad = 95;
    CHECK(fnv::UnderPressure(memory, thresholds));
    memory.dwMemoryLoad = 50; memory.ullAvailVirtual = uint64_t(32) << 20;
    CHECK(fnv::UnderPressure(memory, thresholds));
    CHECK(DeleteFileW(path.c_str()));
    std::puts("PASS async bypass, writable-sharing bypass, concurrent handles, trim, pressure and close cleanup");
    return 0;
}
