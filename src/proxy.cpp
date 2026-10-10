#include "runtime.hpp"
#include "physics.hpp"
#include "device_hooks.hpp"
#include "window_state.hpp"
#include "audio_focus.hpp"
#include <d3d9.h>
#include <d3d9on12.h>
#include <atomic>
#include <new>
#include <cwchar>
#include <iterator>

namespace {
HMODULE systemD3D9{};
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
FARPROC forwarding[21]{};
const char* const names[] = {
    "D3DPERF_BeginEvent", "D3DPERF_EndEvent", "D3DPERF_GetStatus", "D3DPERF_QueryRepeatFrame",
    "D3DPERF_SetMarker", "D3DPERF_SetOptions", "D3DPERF_SetRegion", "DebugSetLevel", "DebugSetMute",
    "Direct3D9EnableMaximizedWindowedModeShim", "Direct3DShaderValidatorCreate9", "PSGPError", "PSGPSampleTexture",
    "Direct3DCreate9On12", "Direct3DCreate9On12Ex"
};
__declspec(noreturn) void MissingExport() {
    fnv::Log("fatal: required system D3D9 export is unavailable");
    RaiseFailFastException(nullptr, nullptr, 0);
    __assume(0);
}
BOOL CALLBACK Init(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH]{};
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\d3d9.dll");
    // An absolute system path prevents accidentally loading the proxy again.
    systemD3D9 = LoadLibraryW(path);
    for (size_t i = 0; i < std::size(names); ++i)
        forwarding[i] = systemD3D9 ? GetProcAddress(systemD3D9, names[i]) : nullptr;
    const WORD ordinals[] = {16, 17, 18, 19, 22, 23};
    for (size_t i = 0; i < std::size(ordinals); ++i)
        forwarding[std::size(names) + i] = systemD3D9 ? GetProcAddress(systemD3D9, MAKEINTRESOURCEA(ordinals[i])) : nullptr;
    for (auto& function : forwarding) if (!function) function = reinterpret_cast<FARPROC>(&MissingExport);
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Init), &self);
    fnv::Initialize(self);
    return TRUE;
}
void __cdecl Ensure() { InitOnceExecuteOnce(&once, Init, nullptr, nullptr); }
template<class T> T System(const char* name) {
    Ensure();
    return reinterpret_cast<T>(GetProcAddress(systemD3D9, name));
}
IDirect3D9* Native(UINT sdk) {
    auto create = System<IDirect3D9* (WINAPI*)(UINT)>("Direct3DCreate9");
    return create ? create(sdk) : nullptr;
}
HRESULT NativeEx(UINT sdk, IDirect3D9Ex** result) {
    auto create = System<HRESULT (WINAPI*)(UINT, IDirect3D9Ex**)>("Direct3DCreate9Ex");
    return create ? create(sdk, result) : D3DERR_NOTAVAILABLE;
}

class Factory final : public IDirect3D9Ex {
    std::atomic<ULONG> refs_{1};
    IDirect3D9* real_;
    IDirect3D9Ex* ex_;
    UINT sdk_;
    bool on12_;
    bool created_ = false;
public:
    Factory(IDirect3D9* real, IDirect3D9Ex* ex, UINT sdk, bool on12) : real_(real), ex_(ex), sdk_(sdk), on12_(on12) {}
    ~Factory() { real_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDirect3D9)) *result = static_cast<IDirect3D9*>(this);
        else if (iid == __uuidof(IDirect3D9Ex) && ex_) *result = static_cast<IDirect3D9Ex*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* init) override { return real_->RegisterSoftwareDevice(init); }
    UINT STDMETHODCALLTYPE GetAdapterCount() override {
        const UINT count=real_->GetAdapterCount();
        return count;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT a, DWORD f, D3DADAPTER_IDENTIFIER9* p) override {
        const HRESULT result=real_->GetAdapterIdentifier(a,f,p);
        return result;
    }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT a, D3DFORMAT f) override {
        const UINT count=real_->GetAdapterModeCount(a,f);
        return count;
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT a, D3DFORMAT f, UINT m, D3DDISPLAYMODE* p) override { return real_->EnumAdapterModes(a, f, m, p); }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* p) override { return real_->GetAdapterDisplayMode(a, p); }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT a, D3DDEVTYPE t, D3DFORMAT d, D3DFORMAT b, BOOL w) override { return real_->CheckDeviceType(a, t, d, b, w); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT a, D3DDEVTYPE t, D3DFORMAT d, DWORD u, D3DRESOURCETYPE r, D3DFORMAT f) override { return real_->CheckDeviceFormat(a, t, d, u, r, f); }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT a, D3DDEVTYPE t, D3DFORMAT f, BOOL w, D3DMULTISAMPLE_TYPE m, DWORD* q) override { return real_->CheckDeviceMultiSampleType(a, t, f, w, m, q); }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT a, D3DDEVTYPE t, D3DFORMAT d, D3DFORMAT r, D3DFORMAT s) override { return real_->CheckDepthStencilMatch(a, t, d, r, s); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT a, D3DDEVTYPE t, D3DFORMAT s, D3DFORMAT d) override { return real_->CheckDeviceFormatConversion(a, t, s, d); }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS9* p) override { return real_->GetDeviceCaps(a, t, p); }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT a) override { return real_->GetAdapterMonitor(a); }
    void Prepare(HWND focus, D3DPRESENT_PARAMETERS& p) {
        fnv::graphics::Prepare(focus, p);
    }
    void Report(IDirect3DDevice9* device) {
        IDirect3DDevice9On12* bridge{};
        const HRESULT result = device->QueryInterface(__uuidof(IDirect3DDevice9On12), reinterpret_cast<void**>(&bridge));
        fnv::Log("device created backend=%s", SUCCEEDED(result) ? "D3D9On12 (DX12 confirmed)" : "native DX9");
        if (bridge) bridge->Release();
        IDirect3DSwapChain9* swapchain{};
        if (SUCCEEDED(device->GetSwapChain(0, &swapchain))) {
            D3DPRESENT_PARAMETERS actual{};
            if (SUCCEEDED(swapchain->GetPresentParameters(&actual)))
                fnv::Log("device actual presentation=%s size=%ux%u vsyncInterval=0x%08lX refresh=%u",
                    actual.Windowed ? "windowed" : "exclusive fullscreen", actual.BackBufferWidth, actual.BackBufferHeight,
                    actual.PresentationInterval, actual.FullScreen_RefreshRateInHz);
            swapchain->Release();
        }
        created_ = true;
    }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT a, D3DDEVTYPE t, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS* p, IDirect3DDevice9** output) override {
        if (!p || !output) return D3DERR_INVALIDCALL;
        *output = nullptr;
        const D3DPRESENT_PARAMETERS original = *p;
        D3DPRESENT_PARAMETERS candidate = original;
        Prepare(focus, candidate);
        HRESULT result = real_->CreateDevice(a, t, focus, flags, &candidate, output);
        if (FAILED(result) && on12_ && !created_) {
            fnv::Log("DX12 CreateDevice failed hr=0x%08lX; trying DX9", static_cast<unsigned long>(result));
            IDirect3D9* fallback = nullptr;
            IDirect3D9Ex* fallbackEx = nullptr;
            if (ex_) {
                if (SUCCEEDED(NativeEx(sdk_, &fallbackEx))) fallback = fallbackEx;
            } else fallback = Native(sdk_);
            if (fallback) {
                candidate = original;
                Prepare(focus, candidate);
                result = fallback->CreateDevice(a, t, focus, flags, &candidate, output);
                if (SUCCEEDED(result)) {
                    real_->Release(); real_ = fallback; ex_ = fallbackEx; on12_ = false;
                } else fallback->Release();
            }
        }
        if (SUCCEEDED(result)) { *p = candidate; Report(*output); fnv::graphics::Attach(*output, candidate); }
        return result;
    }
    UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT a, const D3DDISPLAYMODEFILTER* f) override { return ex_ ? ex_->GetAdapterModeCountEx(a, f) : 0; }
    HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT a, const D3DDISPLAYMODEFILTER* f, UINT m, D3DDISPLAYMODEEX* p) override { return ex_ ? ex_->EnumAdapterModesEx(a, f, m, p) : D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT a, D3DDISPLAYMODEEX* p, D3DDISPLAYROTATION* r) override { return ex_ ? ex_->GetAdapterDisplayModeEx(a, p, r) : D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT a, D3DDEVTYPE t, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* fullscreen, IDirect3DDevice9Ex** output) override {
        if (!ex_ || !p || !output) return D3DERR_INVALIDCALL;
        *output = nullptr;
        const D3DPRESENT_PARAMETERS original = *p;
        auto candidate = original;
        Prepare(focus, candidate);
        auto* mode = candidate.Windowed ? nullptr : fullscreen;
        HRESULT result = ex_->CreateDeviceEx(a, t, focus, flags, &candidate, mode, output);
        if (FAILED(result) && on12_ && !created_) {
            fnv::Log("DX12 CreateDeviceEx failed hr=0x%08lX; trying DX9", static_cast<unsigned long>(result));
            IDirect3D9Ex* fallback{};
            if (SUCCEEDED(NativeEx(sdk_, &fallback))) {
                candidate = original;
                Prepare(focus, candidate);
                result = fallback->CreateDeviceEx(a, t, focus, flags, &candidate, candidate.Windowed ? nullptr : fullscreen, output);
                if (SUCCEEDED(result)) { real_->Release(); real_ = fallback; ex_ = fallback; on12_ = false; }
                else fallback->Release();
            }
        }
        if (SUCCEEDED(result)) { *p = candidate; Report(*output); fnv::graphics::Attach(*output, candidate); }
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT a, LUID* p) override { return ex_ ? ex_->GetAdapterLUID(a, p) : D3DERR_INVALIDCALL; }
};
}

extern "C" IDirect3D9* WINAPI ProxyCreate9(UINT sdk) {
    Ensure();
    if (!fnv::IsGraphicsHost()) return Native(sdk);
    if (fnv::IsGameProcess()) {
        fnv::window::Bind(GetModuleHandleW(nullptr));
        fnv::physics::Install(GetModuleHandleW(nullptr));
        fnv::window::Enforce();
        fnv::audio::BindEngine(GetModuleHandleW(nullptr));
    }
    IDirect3D9* real{};
    bool on12 = false;
    if (fnv::Settings().dx12) {
        auto create = System<PFN_Direct3DCreate9On12>("Direct3DCreate9On12");
        if (create) {
            D3D9ON12_ARGS args{};
            args.Enable9On12 = TRUE;
            real = create(sdk, &args, 1);
            on12 = real != nullptr;
        }
    }
    if (!real) { real = Native(sdk); fnv::Log("factory: native DX9 selected"); }
    if (!real) return nullptr;
    auto* factory = new (std::nothrow) Factory(real, nullptr, sdk, on12);
    if (!factory) real->Release();
    return factory;
}
extern "C" HRESULT WINAPI ProxyCreate9Ex(UINT sdk, IDirect3D9Ex** output) {
    Ensure();
    if (!output) return D3DERR_INVALIDCALL;
    *output = nullptr;
    if (!fnv::IsGraphicsHost()) return NativeEx(sdk, output);
    if (fnv::IsGameProcess()) {
        fnv::window::Bind(GetModuleHandleW(nullptr));
        fnv::physics::Install(GetModuleHandleW(nullptr));
        fnv::window::Enforce();
        fnv::audio::BindEngine(GetModuleHandleW(nullptr));
    }
    IDirect3D9Ex* real{};
    HRESULT result = D3DERR_NOTAVAILABLE;
    bool on12 = false;
    if (fnv::Settings().dx12) {
        auto create = System<PFN_Direct3DCreate9On12Ex>("Direct3DCreate9On12Ex");
        if (create) {
            D3D9ON12_ARGS args{};
            args.Enable9On12 = TRUE;
            result = create(sdk, &args, 1, &real);
            on12 = SUCCEEDED(result);
        }
    }
    if (!real) result = NativeEx(sdk, &real);
    if (FAILED(result)) return result;
    auto* factory = new (std::nothrow) Factory(real, real, sdk, on12);
    if (!factory) { real->Release(); return E_OUTOFMEMORY; }
    *output = factory;
    return result;
}
// ABI-preserving trampolines for system exports, including private ordinal exports.
// Save all registers and flags while resolving the system DLL outside DllMain.
#define FORWARD(name, index) extern "C" __declspec(naked) void name() { \
    __asm { pushfd } __asm { pushad } __asm { call Ensure } \
    __asm { popad } __asm { popfd } __asm { jmp dword ptr [forwarding + index * 4] } }
FORWARD(FwdBeginEvent, 0)
FORWARD(FwdEndEvent, 1)
FORWARD(FwdGetStatus, 2)
FORWARD(FwdQueryRepeatFrame, 3)
FORWARD(FwdSetMarker, 4)
FORWARD(FwdSetOptions, 5)
FORWARD(FwdSetRegion, 6)
FORWARD(FwdDebugSetLevel, 7)
FORWARD(FwdDebugSetMute, 8)
FORWARD(FwdMaximizedShim, 9)
FORWARD(FwdShaderValidator, 10)
FORWARD(FwdPSGPError, 11)
FORWARD(FwdPSGPSampleTexture, 12)
FORWARD(FwdCreate9On12, 13)
FORWARD(FwdCreate9On12Ex, 14)
FORWARD(FwdOrdinal16, 15)
FORWARD(FwdOrdinal17, 16)
FORWARD(FwdOrdinal18, 17)
FORWARD(FwdOrdinal19, 18)
FORWARD(FwdOrdinal22, 19)
FORWARD(FwdOrdinal23, 20)

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
