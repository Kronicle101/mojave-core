#include "audio_focus.hpp"
#include "audio_transport.hpp"
#include <dshow.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH, directory) && GetTempFileNameW(directory, L"FNR", 0, path));
    constexpr DWORD samples = 48000 * 8;
    struct Wave { char riff[4]; DWORD size; char wave[4], fmt[4]; DWORD fmtSize;
        WORD format, channels; DWORD rate, byteRate; WORD align, bits; char data[4]; DWORD dataSize; };
    static_assert(sizeof(Wave) == 44);
    Wave header{{'R','I','F','F'}, samples * 2 + 36, {'W','A','V','E'}, {'f','m','t',' '}, 16,
        1, 1, 48000, 96000, 2, 16, {'d','a','t','a'}, samples * 2};
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    DWORD written{}; std::vector<short> silence(samples, 0);
    CHECK(WriteFile(file, &header, sizeof(header), &written, nullptr) && written == sizeof(header));
    CHECK(WriteFile(file, silence.data(), samples * 2, &written, nullptr) && written == samples * 2);
    CHECK(CloseHandle(file));
    IGraphBuilder* graph{}; IBaseFilter* renderer{}; IMediaControl* control{}; IMediaSeeking* seeking{};
    const GUID nullRenderer{0xC1F400A4, 0x3F08, 0x11D3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
    CHECK(SUCCEEDED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph))));
    CHECK(SUCCEEDED(CoCreateInstance(nullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&renderer))));
    CHECK(SUCCEEDED(graph->AddFilter(renderer, L"Silent transport test sink")));
    CHECK(SUCCEEDED(graph->RenderFile(path, nullptr)));
    CHECK(SUCCEEDED(graph->QueryInterface(IID_PPV_ARGS(&control))));
    CHECK(SUCCEEDED(graph->QueryInterface(IID_PPV_ARGS(&seeking))));
    LONGLONG start = 2 * 10000000LL;
    CHECK(SUCCEEDED(seeking->SetPositions(&start, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning)));
    CHECK(SUCCEEDED(fnv::audio::ApplyTransportRun(control, false)));
    OAFilterState state{}; CHECK(SUCCEEDED(control->GetState(2000, &state)) && state == State_Running);
    CHECK(SUCCEEDED(fnv::audio::CheckpointPause(control, true)));
    CHECK(SUCCEEDED(control->GetState(2000, &state)) && state == State_Stopped);
    LONGLONG stopped{}; CHECK(SUCCEEDED(seeking->GetCurrentPosition(&stopped)) && stopped >= start);
    LONGLONG changed=6*10000000LL;
    CHECK(SUCCEEDED(seeking->SetPositions(&changed,AM_SEEKING_AbsolutePositioning,nullptr,AM_SEEKING_NoPositioning)));
    CHECK(SUCCEEDED(fnv::audio::CheckpointPause(control, true))); // Never replace the original checkpoint.
    CHECK(SUCCEEDED(fnv::audio::CheckpointRun(control, true)));
    CHECK(SUCCEEDED(control->GetState(0, &state)) && state == State_Stopped);
    CHECK(SUCCEEDED(fnv::audio::CheckpointRun(control, false)));
    CHECK(SUCCEEDED(control->GetState(2000, &state)) && state == State_Running);
    LONGLONG resumed{}; CHECK(SUCCEEDED(seeking->GetCurrentPosition(&resumed)));
    CHECK(std::abs(resumed-stopped)<1000000LL); // Restored within 100 ms, not the later seek.
    CHECK(SUCCEEDED(control->Stop()));
    seeking->Release(); control->Release(); renderer->Release(); graph->Release();
    CHECK(DeleteFileW(path)); CoUninitialize();
    fnv::audio::ForgetCheckpoint(control);
    std::puts("PASS real DirectShow graph Stop, background Run suppression and original cursor restoration after an intervening seek");
}
