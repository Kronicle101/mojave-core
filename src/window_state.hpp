#pragma once
#include "runtime.hpp"
namespace fnv::window {
bool Bind(HMODULE executable);
void Enforce();
void HookWindow(HWND window);
// Bounded parser for the documented x86 INI linked-list layout.
DWORD *FindNumericSetting(void *collection, const char *name);
DWORD *FindVerifiedFullscreen(BYTE *image, size_t size);
} // namespace fnv::window
