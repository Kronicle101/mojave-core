#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s error=%lu\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
using ReadFunction = BOOL (WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
ReadFunction nativeRead{};
BOOL WINAPI OtherReadHook(HANDLE h, LPVOID p, DWORD n, LPDWORD r, LPOVERLAPPED o) { return nativeRead(h, p, n, r, o); }
void** ReadSlot() {
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; desc->Name; ++desc) {
        if (!desc->OriginalFirstThunk) continue;
        auto* name = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* address = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; name->u1.AddressOfData; ++name, ++address) {
            if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal)) continue;
            auto* symbol = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + name->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(symbol->Name), "ReadFile") == 0)
                return reinterpret_cast<void**>(&address->u1.Function);
        }
    }
    return nullptr;
}
int wmain(int argc, wchar_t** argv) {
    CHECK(argc >= 2);
    const bool conflict = argc > 2;
    void** slot = ReadSlot();
    CHECK(slot);
    nativeRead = reinterpret_cast<ReadFunction>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReadFile"));
    if (conflict) {
        DWORD protection{}, ignored{};
        CHECK(VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection));
        *slot = reinterpret_cast<void*>(&OtherReadHook);
        CHECK(VirtualProtect(slot, sizeof(void*), protection, &ignored));
    }
    HMODULE dll = LoadLibraryW(argv[1]);
    CHECK(dll);
    auto options = reinterpret_cast<void (WINAPI*)(DWORD)>(GetProcAddress(dll, "D3DPERF_SetOptions"));
    CHECK(options); options(0);
    HMODULE owner{};
    CHECK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(*slot), &owner));
    CHECK(conflict ? owner == GetModuleHandleW(nullptr) : owner == dll);
    wchar_t temp[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH, temp));
    const auto filePath = std::wstring(temp) + L"fnv-import-test-" + std::to_wstring(GetCurrentProcessId()) + L".bsa";
    BYTE expected[8192]; for (DWORD i = 0; i < sizeof(expected); ++i) expected[i] = static_cast<BYTE>(i * 17);
    HANDLE writer = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    CHECK(writer != INVALID_HANDLE_VALUE);
    DWORD count{};
    CHECK(WriteFile(writer, expected, sizeof(expected), &count, nullptr) && count == sizeof(expected));
    CHECK(CloseHandle(writer));
    HANDLE reader = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(reader != INVALID_HANDLE_VALUE);
    BYTE actual[4096];
    for (int i = 0; i < 100; ++i) {
        LARGE_INTEGER position{}; position.QuadPart = 128;
        CHECK(SetFilePointerEx(reader, position, nullptr, FILE_BEGIN));
        CHECK(ReadFile(reader, actual, sizeof(actual), &count, nullptr));
        CHECK(count == sizeof(actual) && memcmp(actual, expected + 128, count) == 0);
    }
    LONG high{};
    CHECK(SetFilePointer(reader, 4096, &high, FILE_BEGIN) == 4096);
    CHECK(ReadFile(reader, actual, sizeof(actual), &count, nullptr));
    CHECK(count == sizeof(actual) && memcmp(actual, expected + 4096, count) == 0);
    CHECK(CloseHandle(reader));
    char ansi[MAX_PATH]{};
    CHECK(WideCharToMultiByte(CP_ACP, 0, filePath.c_str(), -1, ansi, MAX_PATH, nullptr, nullptr));
    reader = CreateFileA(ansi, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(reader != INVALID_HANDLE_VALUE);
    CHECK(ReadFile(reader, actual, sizeof(actual), &count, nullptr));
    CHECK(count == sizeof(actual) && memcmp(actual, expected, count) == 0);
    CHECK(CloseHandle(reader));
    CHECK(DeleteFileW(filePath.c_str()));
    std::printf("PASS real DLL import %s, CreateFileA/W, ReadFile, both seeks and CloseHandle\n", conflict ? "conflict opt-out" : "installation");
    return 0;
}
