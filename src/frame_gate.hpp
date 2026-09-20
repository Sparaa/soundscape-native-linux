// FrameGate — "is the compositor still taking our frames?" On Wayland a compositor stops answering frame callbacks
// for a window that is hidden (covered, minimised, on another workspace), and the driver's FIFO present then blocks the
// calling thread until the window is visible again. That froze the whole main loop — the player never fetched the
// next song and playback stopped at the end of the track. The gate requests its own frame callback on the commit a
// present makes; until the compositor answers it the app skips rendering (no acquire, no present) but keeps polling
// events, ticking the player and draining the main queue. Off Wayland (or built without libwayland-client) it is inert.
#pragma once
#include <cstdint>
struct GLFWwindow;
struct wl_surface;
struct wl_callback;

namespace ss {

class FrameGate {
 public:
  void init(GLFWwindow* window);                 // no-op unless the window is a Wayland surface
  void destroy();
  bool active() const { return surface_ != nullptr; }
  void arm(double now);                          // call right BEFORE a present: attaches the callback to that commit
  void disarm();                                 // the present did not commit after all
  bool pending() const { return pending_; }      // true = the last presented frame has not been consumed yet
  double pendingFor(double now) const { return pending_ ? now - since_ : 0.0; }
  long fired = 0;                                // callbacks answered (diagnostics)
  long skipped = 0;                              // loop iterations that skipped rendering (diagnostics)
  static void onDone(void* data, wl_callback* cb, uint32_t time);   // wl_callback listener (public for the C table)

 private:
  wl_surface* surface_ = nullptr; wl_callback* cb_ = nullptr; bool pending_ = false; double since_ = 0;
};

} // namespace ss
