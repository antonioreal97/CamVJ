#pragma once

#include <windows.h>

namespace atemfx {

// Lets the Direct3D 11 UI layer see window messages without putting a Win32
// concept into the portable Window interface. Registered by UiLayerD3D11,
// called by Win32Window's window procedure. Returns true when the message was
// consumed.
using Win32MessageHook = bool (*)(HWND, UINT, WPARAM, LPARAM);

void setWin32MessageHook(Win32MessageHook hook);

} // namespace atemfx
