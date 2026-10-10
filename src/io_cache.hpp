#pragma once
#include "runtime.hpp"

namespace fnv::io {
struct Stats {
    uint64_t hits, misses, servedBytes, diskBytes, evictions;
    DWORD allocatedBytes, trackedHandles;
};
void Configure(const Config &config);
bool InstallImports(HMODULE executable);
void Trim();
Stats Snapshot();
// Functions shared by the native import hooks and the contract tests.
HANDLE WINAPI OpenA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
HANDLE WINAPI OpenW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL WINAPI Read(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI Close(HANDLE);
DWORD WINAPI Seek(HANDLE, LONG, PLONG, DWORD);
BOOL WINAPI SeekEx(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
} // namespace fnv::io
