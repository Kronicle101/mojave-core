#include "audio_focus.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
BYTE paused{}; int pauses{}, resumes{};
void __cdecl Pause() { paused = 1; ++pauses; }
void __cdecl Resume() { paused = 0; ++resumes; }
struct Graph { void** table; LONG state = 2; int pauses{}, stops{}, runs{}; HRESULT result = S_OK; };
HRESULT __stdcall GraphPause(void* object) { auto& graph = *static_cast<Graph*>(object); ++graph.pauses; graph.state = 1; return graph.result; }
HRESULT __stdcall GraphStop(void* object) { auto& graph = *static_cast<Graph*>(object); ++graph.stops; graph.state = 0; return graph.result; }
HRESULT __stdcall GraphRun(void* object) { auto& graph = *static_cast<Graph*>(object); ++graph.runs; graph.state = 2; return graph.result; }
HRESULT __stdcall GraphState(void* object, LONG timeout, LONG* state) { CHECK(timeout == 0); *state = static_cast<Graph*>(object)->state; return S_OK; }
void Call(BYTE* image, uintptr_t source, uintptr_t target) {
    image[source] = 0xE8;
    const int32_t relative = static_cast<int32_t>(target - source - 5);
    memcpy(image + source + 1, &relative, 4);
}
}
int main() {
    bool owned = false;
    fnv::audio::ApplyFocus(false, &paused, Pause, Resume, owned);
    CHECK(paused && owned && pauses == 1);
    fnv::audio::ApplyFocus(false, &paused, Pause, Resume, owned);
    CHECK(pauses == 1); // Duplicate background messages cannot queue another pause.
    fnv::audio::ApplyFocus(true, &paused, Pause, Resume, owned);
    CHECK(!paused && !owned && resumes == 1);
    fnv::audio::ApplyFocus(true, &paused, Pause, Resume, owned);
    CHECK(resumes == 1);
    paused = 1;
    fnv::audio::ApplyFocus(false, &paused, Pause, Resume, owned);
    fnv::audio::ApplyFocus(true, &paused, Pause, Resume, owned);
    CHECK(paused && !owned && pauses == 1 && resumes == 1); // Preserve an engine/user pause.
    void* table[11]{};
    table[7] = reinterpret_cast<void*>(&GraphRun); table[8] = reinterpret_cast<void*>(&GraphPause);
    table[9] = reinterpret_cast<void*>(&GraphStop); table[10] = reinterpret_cast<void*>(&GraphState);
    Graph radio{table};
    CHECK(fnv::audio::ApplyTransportPause(&radio, true) == S_OK);
    CHECK(radio.stops == 1 && radio.pauses == 0 && radio.state == 0);
    CHECK(fnv::audio::ApplyTransportPause(&radio, true) == S_OK && radio.stops == 1);
    CHECK(fnv::audio::ApplyTransportRun(&radio, true) == S_OK && radio.runs == 0 && radio.state == 0);
    CHECK(fnv::audio::ApplyTransportRun(&radio, false) == S_OK && radio.runs == 1 && radio.state == 2);
    Graph ambient{table};
    CHECK(fnv::audio::ApplyTransportPause(&ambient, false) == S_OK && ambient.pauses == 1 && ambient.stops == 0);
    ambient.result = E_FAIL;
    CHECK(fnv::audio::ApplyTransportPause(&ambient, false) == E_FAIL);
    CHECK(fnv::audio::ApplyTransportRun(&ambient, false) == E_FAIL);
    CHECK(fnv::audio::ApplyTransportPause(nullptr, true) == E_POINTER);
    constexpr size_t size = 0xDF0000;
    BYTE* image = static_cast<BYTE*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(image);
    const BYTE pauseBody[] = {0x55, 0x8B, 0xEC, 0xE8, 0, 0, 0, 0, 0xC6, 0x05, 0, 0, 0, 0, 1, 0xE8, 0, 0, 0, 0, 0x5D, 0xC3};
    memcpy(image + 0x430640, pauseBody, sizeof(pauseBody));
    memcpy(image + 0x430660, pauseBody, sizeof(pauseBody));
    image[0x430660 + 14] = 0;
    const DWORD state = reinterpret_cast<DWORD>(image + 0xDDD312);
    for (uintptr_t function : {uintptr_t(0x430640), uintptr_t(0x430660)}) {
        Call(image, function + 3, 0x430470); Call(image, function + 15, 0x430490);
        memcpy(image + function + 10, &state, 4);
    }
    const BYTE lockPrefix[] = {0x55, 0x8B, 0xEC, 0x6A, 0x00, 0xB9};
    const BYTE unlockPrefix[] = {0x55, 0x8B, 0xEC, 0xB9};
    memcpy(image + 0x430470, lockPrefix, sizeof(lockPrefix));
    memcpy(image + 0x430490, unlockPrefix, sizeof(unlockPrefix));
    const DWORD critical = reinterpret_cast<DWORD>(image + 0xDDD3D8);
    memcpy(image + 0x430476, &critical, 4); memcpy(image + 0x430494, &critical, 4);
    Call(image, 0x43047A, 0x538A0); Call(image, 0x430498, 0x538C0);
    const BYTE pauseSite[] = {0x52, 0x8B, 0x41, 0x20, 0xFF, 0xD0, 0xEB, 0x23};
    const BYTE runSite[] = {0x50, 0x8B, 0x4A, 0x1C, 0xFF, 0xD1};
    const BYTE loopSite[] = {0x52, 0x8B, 0x41, 0x1C, 0xFF, 0xD0};
    memcpy(image + 0x43193A, pauseSite, sizeof(pauseSite));
    memcpy(image + 0x43195F, runSite, sizeof(runSite));
    memcpy(image + 0x431A65, loopSite, sizeof(loopSite));
    const BYTE releaseSite[] = {0x51,0x8B,0x50,0x08,0xFF,0xD2};
    memcpy(image + 0x431CDA, releaseSite, sizeof(releaseSite));
    CHECK(fnv::audio::VerifyEngine(image, size));
    image[0x431A68] ^= 1; // A previous hook/mismatched worker rejects all transport sites.
    CHECK(!fnv::audio::VerifyEngine(image, size)); image[0x431A68] ^= 1;
    image[0x430650] ^= 1;
    CHECK(!fnv::audio::VerifyEngine(image, size));
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    std::puts("PASS engine pause ownership, radio worker Stop/Run ABI, ambient Pause, transport errors and worker/function signature guards");
    return 0;
}
