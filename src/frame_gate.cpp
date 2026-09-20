#include "frame_gate.hpp"
#include <GLFW/glfw3.h>
#ifdef SOUNDSCAPE_WAYLAND_CLIENT
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include <wayland-client.h>
#endif

namespace ss {

#ifdef SOUNDSCAPE_WAYLAND_CLIENT
static const wl_callback_listener kListener = { &FrameGate::onDone };

void FrameGate::init(GLFWwindow* window) {
  if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) return;
  surface_ = glfwGetWaylandWindow(window);
}
void FrameGate::destroy() { if (cb_) wl_callback_destroy(cb_); cb_ = nullptr; surface_ = nullptr; pending_ = false; }
void FrameGate::arm(double now) {
  if (!surface_) return;
  if (cb_) wl_callback_destroy(cb_);
  cb_ = wl_surface_frame(surface_);
  wl_callback_add_listener(cb_, &kListener, this);
  pending_ = true; since_ = now;
}
void FrameGate::disarm() { if (cb_) wl_callback_destroy(cb_); cb_ = nullptr; pending_ = false; }
void FrameGate::onDone(void* data, wl_callback* cb, uint32_t) {
  auto* g = static_cast<FrameGate*>(data);
  wl_callback_destroy(cb);
  if (g->cb_ == cb) g->cb_ = nullptr;
  g->pending_ = false; g->fired++;
}
#else
void FrameGate::init(GLFWwindow*) {}
void FrameGate::destroy() {}
void FrameGate::arm(double) {}
void FrameGate::disarm() {}
void FrameGate::onDone(void*, wl_callback*, uint32_t) {}
#endif

} // namespace ss
