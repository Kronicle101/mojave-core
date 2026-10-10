#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s (error=%lu)\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CHECK(argc >= 2);
    HMODULE dll = LoadLibraryW(argv[1]);
    CHECK(dll);
    auto create = reinterpret_cast<IDirect3D9* (WINAPI*)(UINT)>(GetProcAddress(dll, "Direct3DCreate9"));
    auto createEx = reinterpret_cast<HRESULT (WINAPI*)(UINT, IDirect3D9Ex**)>(GetProcAddress(dll, "Direct3DCreate9Ex"));
    auto options = reinterpret_cast<void (WINAPI*)(DWORD)>(GetProcAddress(dll, "D3DPERF_SetOptions"));
    auto begin = reinterpret_cast<int (WINAPI*)(D3DCOLOR, LPCWSTR)>(GetProcAddress(dll, "D3DPERF_BeginEvent"));
    auto end = reinterpret_cast<int (WINAPI*)()>(GetProcAddress(dll, "D3DPERF_EndEvent"));
    CHECK(create && createEx && options && begin && end);
    options(0); begin(0, L"Stabilizer ABI test"); end();
    CHECK(!create(0)); // Unsupported SDK version must remain rejected.
    IDirect3D9* d3d = create(D3D_SDK_VERSION);
    CHECK(d3d && d3d->GetAdapterCount() > 0);
    IUnknown* identity{};
    CHECK(SUCCEEDED(d3d->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&identity))));
    CHECK(identity == static_cast<IUnknown*>(d3d));
    identity->Release();
    IDirect3D9Ex* unavailable{};
    CHECK(FAILED(d3d->QueryInterface(__uuidof(IDirect3D9Ex), reinterpret_cast<void**>(&unavailable))));
    HWND window = CreateWindowExW(0, L"STATIC", L"FNV Stabilizer hidden graphics test", WS_OVERLAPPEDWINDOW,
        0, 0, 320, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(window);
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = 320; parameters.BackBufferHeight = 240;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.Windowed = TRUE;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device{};
    HRESULT result = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &parameters, &device);
    std::printf("CreateDevice HRESULT=0x%08lX\n", static_cast<unsigned long>(result));
    CHECK(SUCCEEDED(result) && device);
    IDirect3DDevice9On12* bridge{};
    result = device->QueryInterface(__uuidof(IDirect3DDevice9On12), reinterpret_cast<void**>(&bridge));
    std::printf("Actual backend: %s\n", SUCCEEDED(result) ? "D3D9On12 / DirectX 12" : "DirectX 9 fallback");
    if (argc >= 3) CHECK((wcscmp(argv[2], L"1") == 0) == SUCCEEDED(result));
    if (bridge) bridge->Release();
    CHECK(SUCCEEDED(device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 40, 60), 1.0f, 0)));
    std::puts("stage Clear passed");
    CHECK(SUCCEEDED(device->BeginScene()));
    CHECK(SUCCEEDED(device->EndScene()));
    if (argc >= 4) {
        SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
        SendMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
        CHECK(!IsWindowVisible(window)); // The diagnostic host must remain hidden.
        std::puts("stage native focus message chain passed");
    }
    result = device->Present(nullptr, nullptr, nullptr, nullptr);
    CHECK(SUCCEEDED(result) || result == D3DERR_DEVICELOST);
    // Emulate FNV asking for exclusive mode again after an Alt+Tab reset.
    parameters.Windowed = FALSE;
    parameters.FullScreen_RefreshRateInHz = 60;
    CHECK(SUCCEEDED(device->Reset(&parameters)));
    std::puts("stage Reset passed");
    CHECK(parameters.Windowed && parameters.FullScreen_RefreshRateInHz == 0);
    if (argc >= 4) {
        SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
        SendMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
    }
    device->Release(); d3d->Release();
    std::puts("stage D3D9 release passed");
    IDirect3D9Ex* ex{};
    CHECK(SUCCEEDED(createEx(D3D_SDK_VERSION, &ex)) && ex);
    IDirect3DDevice9Ex* deviceEx{};
    CHECK(SUCCEEDED(ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &parameters, nullptr, &deviceEx)) && deviceEx);
    CHECK(SUCCEEDED(deviceEx->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)));
    parameters.Windowed = FALSE;
    parameters.FullScreen_RefreshRateInHz = 60;
    CHECK(SUCCEEDED(deviceEx->ResetEx(&parameters, nullptr)));
    CHECK(parameters.Windowed && parameters.FullScreen_RefreshRateInHz == 0);
    deviceEx->Release(); ex->Release();
    CHECK(DestroyWindow(window));
    CHECK(FreeLibrary(dll));
    std::puts("PASS proxy exports, COM identity, rendering and forced-windowed Reset/ResetEx recovery");
    return 0;
}
