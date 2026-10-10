# Source verification

This game's project builds independently with MSVC Win32 and /W4 /WX.
All 12 registered checks passed on 2026-10-10.

The checks include file caching, import conflicts, presentation/focus, fractional timing, native physics accumulation and audio transport. The reported ragdoll scene and NPC lips were confirmed in-game; broader gameplay and the small weapon-model tremor remain outside that result.

Runtime statements were compared against the frozen release source after resolving the game switch and accounting for renamed internal identifiers. Engine addresses, calling conventions and D3D9 exports are retained. Published DLL files were not replaced.
