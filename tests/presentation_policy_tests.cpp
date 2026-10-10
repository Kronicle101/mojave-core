#include "device_hooks.hpp"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
    fnv::Config config;
    config.borderless = false; config.respectVSync = true;
    D3DPRESENT_PARAMETERS p{};
    p.Windowed = FALSE; p.FullScreen_RefreshRateInHz = 144;
    p.BackBufferWidth = 2560; p.BackBufferHeight = 1440;
    p.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    fnv::graphics::ApplyPolicy(config, true, p);
    CHECK(!p.Windowed && p.FullScreen_RefreshRateInHz == 144);
    CHECK(p.BackBufferWidth == 2560 && p.BackBufferHeight == 1440);
    CHECK(p.PresentationInterval == D3DPRESENT_INTERVAL_ONE);
    config.respectVSync = false;
    fnv::graphics::ApplyPolicy(config, true, p);
    CHECK(!p.Windowed && p.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE);
    p.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    fnv::graphics::ApplyPolicy(config, false, p);
    CHECK(p.PresentationInterval == D3DPRESENT_INTERVAL_ONE);
    config.borderless = true;
    fnv::graphics::ApplyPolicy(config, true, p);
    CHECK(p.Windowed && p.FullScreen_RefreshRateInHz == 0);
    config.borderless = false; config.respectVSync = true;
    p.Windowed = TRUE;
    fnv::graphics::ApplyPolicy(config, true, p);
    CHECK(p.Windowed); // Native mode honors windowed too if the user selects it.
    std::puts("PASS native exclusive/refresh/resolution, VSync preservation, optional unlock and borderless separation");
    return 0;
}
