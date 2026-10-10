#pragma once
#include "runtime.hpp"
#include <d3d9.h>

namespace fnv::graphics {
void Prepare(HWND focus, D3DPRESENT_PARAMETERS &parameters);
void ApplyPolicy(const Config &config, bool timingVerified, D3DPRESENT_PARAMETERS &parameters);
bool Attach(IDirect3DDevice9 *device, const D3DPRESENT_PARAMETERS &parameters);
void Report();
} // namespace fnv::graphics
