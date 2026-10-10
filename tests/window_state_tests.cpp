#include "window_state.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    struct Setting { void* table; DWORD value; const char* name; };
    struct Node { Setting* setting; Node* next; };
    std::array<BYTE, 0x114> collection{};
    Setting other{nullptr, 42, "iSize W:Display"};
    Setting fullscreen{nullptr, 1, "bFull Screen:Display"};
    Setting interval{nullptr, 1, "iPresentInterval:Display"};
    Node tail{&interval, nullptr};
    Node middle{&fullscreen, &tail};
    const Node head{&other, &middle};
    memcpy(collection.data() + 0x10C, &head, sizeof(head));
    auto* value = fnv::window::FindNumericSetting(collection.data(), "bFull Screen:Display");
    CHECK(value == &fullscreen.value);
    *value = 0;
    CHECK(fullscreen.value == 0 && other.value == 42 && interval.value == 1);
    CHECK(fnv::window::FindNumericSetting(collection.data(), "IPRESENTINTERVAL:DISPLAY") == &interval.value);
    CHECK(!fnv::window::FindNumericSetting(collection.data(), "missing"));
    tail.next = &middle; // Corrupt/cyclic lists must terminate without touching unrelated data.
    CHECK(!fnv::window::FindNumericSetting(collection.data(), "missing"));
    CHECK(!fnv::window::FindNumericSetting(reinterpret_cast<void*>(1), "bFull Screen:Display"));
    // The actual fullscreen setting is not registered in the INI list. Its
    // getter, object identity, name and data must all match before it is used.
    constexpr size_t size = 0xE68000;
    auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(image);
    const BYTE getter[] = {0x55, 0x8B, 0xEC, 0xB9, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x8A, 0x00, 0x5D, 0xC3};
    const BYTE valueCode[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF8, 0x83, 0x7D, 0xF8, 0x00,
        0x74, 0x0B, 0x8B, 0x45, 0xF8, 0x83, 0xC0, 0x04};
    memcpy(image + 0x46E10, getter, sizeof(getter));
    memcpy(image + 0x8D60, valueCode, sizeof(valueCode));
    const DWORD object = reinterpret_cast<DWORD>(image + 0xDC77B4);
    const int32_t relative = static_cast<int32_t>(0x8D60 - (0x46E10 + 13));
    memcpy(image + 0x46E14, &object, 4); memcpy(image + 0x46E19, &relative, 4);
    auto* nativeSetting = reinterpret_cast<Setting*>(image + 0xDC77B4);
    strcpy_s(reinterpret_cast<char*>(image + 0xC21984), 64, "bFull Screen:Display");
    *nativeSetting = {nullptr, 1, reinterpret_cast<const char*>(image + 0xC21984)};
    CHECK(fnv::window::FindVerifiedFullscreen(image, size) == &nativeSetting->value);
    image[0x46E10] = 0xE9;
    CHECK(!fnv::window::FindVerifiedFullscreen(image, size));
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    std::puts("PASS bounded x86 setting lookup, exact named target, unrelated preservation and invalid/cyclic lists");
    return 0;
}
