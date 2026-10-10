# Credits

Distributed under GNU LGPL 2.1; see LICENSE.

The timing integration is adapted from New Vegas Tick Fix by Carxt, lStewieAl
and jazzisparis, commit f442e43d5b82cd229d5ea84ff181142e8a233ec6, under LGPL 2.1.
NVTF also credits tgspy for QA. Original project:
https://github.com/carxt/New-Vegas-Tick-Fix

The adaptation uses native DLL loading, verified engine hooks, a fractional
clock and project-specific focus, audio and cache integration. The NVTF binary
is not distributed or required.

Audio layout references were consulted in JIP LN NVSE GameSound.h; that code
is not compiled or included:
https://github.com/jazzisparis/JIP-LN-NVSE

Source cleanup and project separation: 2026-10-10. Runtime logic and third-party
attributions are retained. Windows supplies D3D9, D3D9On12 and D3D12.
