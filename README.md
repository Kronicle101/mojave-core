# Mojave Core

Source code for the Fallout: New Vegas native engine stabilizer.
Version 0.4.4, for the Steam FalloutNV.exe 1.4.0.525.

## Build

Requires MSVC, Windows SDK and CMake. Use a Win32 build:

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output: `build/Release/d3d9.dll`.

## License and credits

GNU LGPL 2.1. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
Timing integration adapted from NVTF by Carxt, lStewieAl and jazzisparis.

This source corresponds to the distributed 0.4.4 DLL with SHA-256:
`F22D0242EB8216C77E0797CF6F5B02CA13EE215953C492D8BBD4DF6034576F80`.

[Mod page](https://www.nexusmods.com/newvegas/mods/99955).