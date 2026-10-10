#include "device_hooks.hpp"
#include "physics.hpp"
#include "window_state.hpp"
#include <array>
#include <atomic>
#include <memory>

namespace fnv::graphics {
namespace {
// Keep the native table and its undocumented virtual entries intact.
struct Patch {
    void **table{};
    void *release{};
    void *reset{};
    void *present{};
    void *resetEx{};
    void *presentEx{};
    bool extended{};
    DWORD users{};
};
struct Context {
    IDirect3DDevice9 *device{};
    std::shared_ptr<Patch> patch;
    HWND window{};
    std::atomic<HRESULT> lastPresent{S_OK};
};
std::array<std::shared_ptr<Context>, 32> devices;
std::array<std::shared_ptr<Patch>, 16> patches;
SRWLOCK lock = SRWLOCK_INIT;
std::atomic<uint64_t> presents{}, resets{}, failedPresents{};
std::shared_ptr<Context> Find(IDirect3DDevice9 *device) {
    AcquireSRWLockShared(&lock);
    std::shared_ptr<Context> result;
    for (const auto &context : devices)
        if (context && context->device == device) {
            result = context;
            break;
        }
    ReleaseSRWLockShared(&lock);
    return result;
}
std::shared_ptr<Patch> ForDevice(IDirect3DDevice9 *device) {
    void **table = *reinterpret_cast<void ***>(device);
    AcquireSRWLockShared(&lock);
    std::shared_ptr<Patch> result;
    for (const auto &patch : patches)
        if (patch && patch->table == table) {
            result = patch;
            break;
        }
    ReleaseSRWLockShared(&lock);
    return result;
}
bool SetSlot(void **slot, void *expected, void *replacement) {
    DWORD protection{};
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection))
        return false;
    const bool changed = InterlockedCompareExchangePointer(slot, replacement, expected) == expected;
    DWORD ignored{};
    VirtualProtect(slot, sizeof(void *), protection, &ignored);
    return changed;
}
ULONG WINAPI Release(IDirect3DDevice9 *);
HRESULT WINAPI Reset(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
HRESULT WINAPI Present(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
HRESULT WINAPI ResetEx(IDirect3DDevice9Ex *, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *);
HRESULT WINAPI PresentEx(IDirect3DDevice9Ex *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
void Restore(Patch &p) {
    SetSlot(p.table + 2, reinterpret_cast<void *>(&Release), p.release);
    SetSlot(p.table + 16, reinterpret_cast<void *>(&Reset), p.reset);
    SetSlot(p.table + 17, reinterpret_cast<void *>(&Present), p.present);
    if (p.extended) {
        SetSlot(p.table + 121, reinterpret_cast<void *>(&PresentEx), p.presentEx);
        SetSlot(p.table + 132, reinterpret_cast<void *>(&ResetEx), p.resetEx);
    }
}
ULONG WINAPI Release(IDirect3DDevice9 *device) {
    const auto context = Find(device);
    const auto patch = context ? context->patch : ForDevice(device);
    if (!patch)
        return 0;
    const ULONG count = reinterpret_cast<ULONG(WINAPI *)(IDirect3DDevice9 *)>(patch->release)(device);
    if (!count && context) {
        AcquireSRWLockExclusive(&lock);
        for (auto &slot : devices)
            if (slot == context) {
                slot.reset();
                break;
            }
        if (--patch->users == 0) {
            Restore(*patch);
            for (auto &slot : patches)
                if (slot == patch) {
                    slot.reset();
                    break;
                }
        }
        ReleaseSRWLockExclusive(&lock);
    }
    return count;
}
HRESULT WINAPI Reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *parameters) {
    const auto context = Find(device);
    const auto patch = context ? context->patch : ForDevice(device);
    if (!patch || !parameters)
        return D3DERR_INVALIDCALL;
    auto candidate = *parameters;
    if (context)
        Prepare(context->window, candidate);
    const HRESULT result = reinterpret_cast<HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *)>(
        patch->reset)(device, &candidate);
    ++resets;
    Log("device Reset windowed=%d interval=0x%08lX hr=0x%08lX", candidate.Windowed,
        candidate.PresentationInterval, static_cast<unsigned long>(result));
    if (SUCCEEDED(result)) {
        *parameters = candidate;
        if (context && candidate.hDeviceWindow)
            context->window = candidate.hDeviceWindow;
    }
    return result;
}
void Presented(const std::shared_ptr<Context> &context, HRESULT result) {
    ++presents;
    if (FAILED(result))
        ++failedPresents;
    if (!context)
        return;
    const HRESULT old = context->lastPresent.exchange(result);
    if (old != result && (FAILED(old) || FAILED(result)))
        Log("device Present status changed old=0x%08lX new=0x%08lX", static_cast<unsigned long>(old),
            static_cast<unsigned long>(result));
}
HRESULT WINAPI Present(IDirect3DDevice9 *device, const RECT *source, const RECT *destination, HWND window,
                       const RGNDATA *dirty) {
    const auto context = Find(device);
    const auto patch = context ? context->patch : ForDevice(device);
    if (!patch)
        return D3DERR_INVALIDCALL;
    const HRESULT result = reinterpret_cast<HRESULT(WINAPI *)(IDirect3DDevice9 *, const RECT *, const RECT *,
                                                              HWND, const RGNDATA *)>(patch->present)(
        device, source, destination, window, dirty);
    Presented(context, result);
    return result;
}
HRESULT WINAPI ResetEx(IDirect3DDevice9Ex *device, D3DPRESENT_PARAMETERS *parameters,
                       D3DDISPLAYMODEEX *mode) {
    const auto context = Find(device);
    const auto patch = context ? context->patch : ForDevice(device);
    if (!patch || !patch->extended || !parameters)
        return D3DERR_INVALIDCALL;
    auto candidate = *parameters;
    if (context)
        Prepare(context->window, candidate);
    const HRESULT result = reinterpret_cast<HRESULT(WINAPI *)(IDirect3DDevice9Ex *, D3DPRESENT_PARAMETERS *,
                                                              D3DDISPLAYMODEEX *)>(patch->resetEx)(
        device, &candidate, candidate.Windowed ? nullptr : mode);
    ++resets;
    Log("device ResetEx windowed=%d hr=0x%08lX", candidate.Windowed, static_cast<unsigned long>(result));
    if (SUCCEEDED(result))
        *parameters = candidate;
    return result;
}
HRESULT WINAPI PresentEx(IDirect3DDevice9Ex *device, const RECT *source, const RECT *destination, HWND window,
                         const RGNDATA *dirty, DWORD flags) {
    const auto context = Find(device);
    const auto patch = context ? context->patch : ForDevice(device);
    if (!patch || !patch->extended)
        return D3DERR_INVALIDCALL;
    const HRESULT result = reinterpret_cast<HRESULT(WINAPI *)(IDirect3DDevice9Ex *, const RECT *,
                                                              const RECT *, HWND, const RGNDATA *, DWORD)>(
        patch->presentEx)(device, source, destination, window, dirty, flags);
    Presented(context, result);
    return result;
}
} // namespace
void ApplyPolicy(const Config &config, bool timingVerified, D3DPRESENT_PARAMETERS &parameters) {
    if (config.borderless) {
        parameters.Windowed = TRUE;
        parameters.FullScreen_RefreshRateInHz = 0;
    }
    if (timingVerified && !config.respectVSync)
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
}
void Prepare(HWND focus, D3DPRESENT_PARAMETERS &parameters) {
    window::Enforce();
    if (!parameters.hDeviceWindow)
        parameters.hDeviceWindow = focus;
    window::HookWindow(parameters.hDeviceWindow);
    ApplyPolicy(Settings(), physics::Active(), parameters);
    if (Settings().borderless) {
        UINT width = parameters.BackBufferWidth, height = parameters.BackBufferHeight;
        ApplyBorderless(parameters.hDeviceWindow, width, height);
    }
}
bool Attach(IDirect3DDevice9 *device, const D3DPRESENT_PARAMETERS &parameters) {
    if (!device)
        return false;
    if (Find(device))
        return true;
    auto context = std::make_shared<Context>();
    context->device = device;
    context->window = parameters.hDeviceWindow;
    void **table = *reinterpret_cast<void ***>(device);
    IDirect3DDevice9Ex *extended{};
    bool isEx = false;
    if (SUCCEEDED(
            device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void **>(&extended)))) {
        isEx = static_cast<IDirect3DDevice9 *>(extended) == device;
        extended->Release();
    }
    AcquireSRWLockExclusive(&lock);
    std::shared_ptr<Context> *freeDevice = nullptr;
    for (auto &slot : devices)
        if (!slot) {
            freeDevice = &slot;
            break;
        }
    if (!freeDevice) {
        ReleaseSRWLockExclusive(&lock);
        return false;
    }
    std::shared_ptr<Patch> patch;
    for (const auto &slot : patches)
        if (slot && slot->table == table) {
            patch = slot;
            break;
        }
    if (!patch) {
        std::shared_ptr<Patch> *freePatch = nullptr;
        for (auto &slot : patches)
            if (!slot) {
                freePatch = &slot;
                break;
            }
        if (!freePatch) {
            ReleaseSRWLockExclusive(&lock);
            return false;
        }
        patch = std::make_shared<Patch>();
        patch->table = table;
        patch->release = table[2];
        patch->reset = table[16];
        patch->present = table[17];
        *freePatch = patch;
        if (!SetSlot(table + 2, patch->release, reinterpret_cast<void *>(&Release)) ||
            !SetSlot(table + 16, patch->reset, reinterpret_cast<void *>(&Reset)) ||
            !SetSlot(table + 17, patch->present, reinterpret_cast<void *>(&Present))) {
            Restore(*patch);
            freePatch->reset();
            ReleaseSRWLockExclusive(&lock);
            return false;
        }
    }
    if (isEx && !patch->extended) {
        patch->presentEx = table[121];
        patch->resetEx = table[132];
        const bool presentSet = SetSlot(table + 121, patch->presentEx, reinterpret_cast<void *>(&PresentEx));
        const bool resetSet =
            presentSet && SetSlot(table + 132, patch->resetEx, reinterpret_cast<void *>(&ResetEx));
        if (!resetSet) {
            if (presentSet)
                SetSlot(table + 121, reinterpret_cast<void *>(&PresentEx), patch->presentEx);
        } else
            patch->extended = true;
    }
    context->patch = patch;
    ++patch->users;
    *freeDevice = context;
    ReleaseSRWLockExclusive(&lock);
    Log("device recovery hooks installed=1 ex=%d", patch->extended);
    return true;
}
void Report() {
    Log("graphics presents=%llu failedPresents=%llu resets=%llu", presents.load(), failedPresents.load(),
        resets.load());
}
} // namespace fnv::graphics
