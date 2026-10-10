#include "io_cache.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>

namespace fnv::io {
namespace {
constexpr DWORD BlockBytes = 256 * 1024;
constexpr size_t MaxBlocks = 256;
struct File {
    HANDLE handle{};
    uint64_t size{};
};
struct Block {
    BYTE *bytes{};
    HANDLE owner{};
    uint64_t offset{}, touched{};
    DWORD size{};
};
std::array<File, 128> files{};
std::array<Block, MaxBlocks> blocks{};
SRWLOCK lock = SRWLOCK_INIT;
Config settings;
Stats stats{};
uint64_t clockValue{};
ULONGLONG pressureCheck{};
bool pressureActive{};
struct Guard {
    Guard() {
        AcquireSRWLockExclusive(&lock);
    }
    ~Guard() {
        ReleaseSRWLockExclusive(&lock);
    }
};
File *Find(HANDLE handle) {
    for (auto &file : files)
        if (file.handle == handle)
            return &file;
    return nullptr;
}
void Release(Block &block) {
    if (block.bytes) {
        VirtualFree(block.bytes, 0, MEM_RELEASE);
        stats.allocatedBytes -= BlockBytes;
        ++stats.evictions;
    }
    block = {};
}
void Clear() {
    for (auto &block : blocks)
        Release(block);
}
bool IsBSA(LPCWSTR path) {
    if (!path)
        return false;
    const size_t length = wcslen(path);
    return length > 4 && _wcsicmp(path + length - 4, L".bsa") == 0;
}
void Track(HANDLE handle, LPCWSTR path, DWORD access, DWORD share, DWORD disposition, DWORD flags) {
    if (!settings.cache || handle == INVALID_HANDLE_VALUE || !IsBSA(path) || access != GENERIC_READ ||
        (share & (FILE_SHARE_WRITE | FILE_SHARE_DELETE)) || disposition != OPEN_EXISTING ||
        (flags & (FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING | FILE_FLAG_DELETE_ON_CLOSE)))
        return;
    if (GetFileType(handle) != FILE_TYPE_DISK)
        return;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return;
    Guard guard;
    // A handle closed outside the executable may be reused. Never reuse its old cache.
    if (auto *old = Find(handle)) {
        for (auto &block : blocks)
            if (block.owner == handle)
                Release(block);
        *old = {};
        --stats.trackedHandles;
    }
    for (auto &file : files)
        if (!file.handle) {
            file = {handle, (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow};
            ++stats.trackedHandles;
            break;
        }
}
bool Pressure() {
    const auto now = GetTickCount64();
    if (now - pressureCheck < 1000)
        return pressureActive;
    pressureCheck = now;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    pressureActive = GlobalMemoryStatusEx(&memory) && UnderPressure(memory, settings);
    return pressureActive;
}
Block *AcquireBlock() {
    const size_t limit = std::min<size_t>(MaxBlocks, (settings.cacheMiB * 1024 * 1024) / BlockBytes);
    Block *oldest = nullptr;
    for (size_t i = 0; i < limit; ++i) {
        auto &block = blocks[i];
        if (!block.bytes) {
            block.bytes = static_cast<BYTE *>(
                VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (!block.bytes)
                return nullptr;
            stats.allocatedBytes += BlockBytes;
            return &block;
        }
        if (!oldest || block.touched < oldest->touched)
            oldest = &block;
    }
    if (oldest) {
        ++stats.evictions;
        oldest->size = 0;
    }
    return oldest;
}
bool CopySafe(void *destination, const void *source, DWORD count) {
    __try {
        memcpy(destination, source, count);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
} // namespace
void Configure(const Config &config) {
    Guard guard;
    Clear();
    files = {};
    settings = config;
    settings.cacheMiB = std::clamp(settings.cacheMiB, DWORD(1), DWORD(64));
    settings.readAheadKiB = std::min(settings.readAheadKiB, DWORD(256));
    stats = {};
    pressureCheck = 0;
    pressureActive = false;
}
void Trim() {
    Guard guard;
    Clear();
}
Stats Snapshot() {
    Guard guard;
    return stats;
}
HANDLE WINAPI OpenW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                    DWORD flags, HANDLE templateFile) {
    HANDLE result = CreateFileW(path, access, share, sa, disposition, flags, templateFile);
    const DWORD error = GetLastError();
    Track(result, path, access, share, disposition, flags);
    SetLastError(error);
    return result;
}
HANDLE WINAPI OpenA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                    DWORD flags, HANDLE templateFile) {
    HANDLE result = CreateFileA(path, access, share, sa, disposition, flags, templateFile);
    const DWORD error = GetLastError();
    wchar_t wide[32768]{};
    if (path && MultiByteToWideChar(CP_ACP, 0, path, -1, wide, static_cast<int>(std::size(wide))))
        Track(result, wide, access, share, disposition, flags);
    SetLastError(error);
    return result;
}
BOOL WINAPI Read(HANDLE handle, LPVOID destination, DWORD count, LPDWORD read, LPOVERLAPPED overlapped) {
    if (overlapped || !read || !destination || !count || count > BlockBytes)
        return ReadFile(handle, destination, count, read, overlapped);
    Guard guard;
    const auto *file = Find(handle);
    if (!file)
        return ReadFile(handle, destination, count, read, nullptr);
    const DWORD initialError = GetLastError();
    LARGE_INTEGER zero{}, position{};
    if (!SetFilePointerEx(handle, zero, &position, FILE_CURRENT))
        return ReadFile(handle, destination, count, read, nullptr);
    const uint64_t offset = static_cast<uint64_t>(position.QuadPart);
    if (Pressure()) {
        Clear();
        return ReadFile(handle, destination, count, read, nullptr);
    }
    const DWORD expected =
        offset >= file->size ? 0 : static_cast<DWORD>(std::min<uint64_t>(count, file->size - offset));
    if (!expected)
        return ReadFile(handle, destination, count, read, nullptr);
    for (auto &block : blocks) {
        if (!block.bytes || block.owner != handle || offset < block.offset ||
            offset - block.offset > block.size)
            continue;
        const uint64_t relative = offset - block.offset;
        if (expected > block.size - relative)
            continue;
        if (!CopySafe(destination, block.bytes + relative, expected)) {
            *read = 0;
            SetLastError(ERROR_NOACCESS);
            return FALSE;
        }
        LARGE_INTEGER next{};
        next.QuadPart = position.QuadPart + expected;
        if (!SetFilePointerEx(handle, next, nullptr, FILE_BEGIN))
            return ReadFile(handle, destination, count, read, nullptr);
        *read = expected;
        block.touched = ++clockValue;
        ++stats.hits;
        stats.servedBytes += expected;
        SetLastError(initialError);
        return TRUE;
    }
    ++stats.misses;
    auto *block = AcquireBlock();
    if (!block)
        return ReadFile(handle, destination, count, read, nullptr);
    block->size = 0;
    const DWORD requested = static_cast<DWORD>(
        std::min<uint64_t>(std::max(count, settings.readAheadKiB * 1024), file->size - offset));
    DWORD got{};
    const BOOL ok = ReadFile(handle, block->bytes, requested, &got, nullptr);
    const DWORD nativeError = GetLastError();
    stats.diskBytes += got;
    const DWORD delivered = std::min(count, got);
    LARGE_INTEGER next{};
    next.QuadPart = position.QuadPart + delivered;
    // Always restore the logical pointer after speculative I/O, including errors.
    if (!ok || got > count) {
        if (!ok)
            next = position;
        if (!SetFilePointerEx(handle, next, nullptr, FILE_BEGIN)) {
            *read = 0;
            return FALSE;
        }
    }
    if (!ok) {
        SetLastError(nativeError);
        return ReadFile(handle, destination, count, read, nullptr);
    }
    if (!CopySafe(destination, block->bytes, delivered)) {
        SetFilePointerEx(handle, position, nullptr, FILE_BEGIN);
        *read = 0;
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    block->owner = handle;
    block->offset = offset;
    block->size = got;
    block->touched = ++clockValue;
    *read = delivered;
    SetLastError(initialError);
    return TRUE;
}
BOOL WINAPI Close(HANDLE handle) {
    Guard guard;
    const BOOL ok = CloseHandle(handle);
    const DWORD error = GetLastError();
    if (ok)
        if (auto *file = Find(handle)) {
            for (auto &block : blocks)
                if (block.owner == handle)
                    Release(block);
            *file = {};
            --stats.trackedHandles;
        }
    SetLastError(error);
    return ok;
}
DWORD WINAPI Seek(HANDLE handle, LONG distance, PLONG high, DWORD method) {
    Guard guard;
    return SetFilePointer(handle, distance, high, method);
}
BOOL WINAPI SeekEx(HANDLE handle, LARGE_INTEGER distance, PLARGE_INTEGER result, DWORD method) {
    Guard guard;
    return SetFilePointerEx(handle, distance, result, method);
}
bool InstallImports(HMODULE executable) {
    struct Hook {
        const char *name;
        void *replacement;
        void **slot{};
        void *original{};
        bool installed{};
    };
    Hook hooks[] = {{"CreateFileA", reinterpret_cast<void *>(&OpenA)},
                    {"CreateFileW", reinterpret_cast<void *>(&OpenW)},
                    {"ReadFile", reinterpret_cast<void *>(&Read)},
                    {"CloseHandle", reinterpret_cast<void *>(&Close)},
                    {"SetFilePointer", reinterpret_cast<void *>(&Seek)},
                    {"SetFilePointerEx", reinterpret_cast<void *>(&SeekEx)}};
    auto *base = reinterpret_cast<BYTE *>(executable);
    const auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        return false;
    const auto &directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress)
        return false;
    auto *descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (_stricmp(reinterpret_cast<const char *>(base + descriptor->Name), "KERNEL32.dll") != 0 ||
            !descriptor->OriginalFirstThunk)
            continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + descriptor->OriginalFirstThunk);
        auto *addresses = reinterpret_cast<IMAGE_THUNK_DATA *>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto *name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            for (auto &hook : hooks)
                if (strcmp(reinterpret_cast<const char *>(name->Name), hook.name) == 0) {
                    hook.slot = reinterpret_cast<void **>(&addresses->u1.Function);
                    hook.original = reinterpret_cast<void *>(
                        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), hook.name));
                }
        }
    }
    // All imports used by this build are required.
    for (const auto &hook : hooks)
        if (!hook.slot || *hook.slot != hook.original) {
            Log("I/O cache skipped: absent or already hooked import %s", hook.name);
            return false;
        }
    bool success = true;
    for (auto &hook : hooks) {
        DWORD protection{};
        if (!VirtualProtect(hook.slot, sizeof(void *), PAGE_READWRITE, &protection)) {
            success = false;
            break;
        }
        hook.installed =
            InterlockedCompareExchangePointer(hook.slot, hook.replacement, hook.original) == hook.original;
        DWORD ignored{};
        VirtualProtect(hook.slot, sizeof(void *), protection, &ignored);
        if (!hook.installed) {
            success = false;
            break;
        }
    }
    if (!success)
        for (auto &hook : hooks)
            if (hook.installed) {
                DWORD protection{};
                if (VirtualProtect(hook.slot, sizeof(void *), PAGE_READWRITE, &protection)) {
                    InterlockedCompareExchangePointer(hook.slot, hook.original, hook.replacement);
                    DWORD ignored{};
                    VirtualProtect(hook.slot, sizeof(void *), protection, &ignored);
                }
            }
    return success;
}
} // namespace fnv::io
