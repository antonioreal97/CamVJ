#pragma once

#include <memory>

namespace atemfx {

class GraphicsDevice;
class UiLayer;
class Window;

// Implemented once per platform. Exactly one backend is compiled into a build,
// so these never have to choose at runtime.
std::unique_ptr<Window>         createPlatformWindow();
std::unique_ptr<GraphicsDevice> createGraphicsDevice();

// The name of the backend, for logs and the UI.
const char* backendName();

} // namespace atemfx
