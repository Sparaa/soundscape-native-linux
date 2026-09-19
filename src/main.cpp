#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app.hpp"

static void usage() {
  std::puts("soundscape-native — Linux/Vulkan client for the Soundscape radio api\n"
            "  --api URL          api base (default http://127.0.0.1:3021, env SOUNDSCAPE_API)\n"
            "  --station ID       open this station at start\n"
            "  --fullscreen       start fullscreen (panel hidden)\n"
            "  --no-vsync         mailbox/immediate present mode\n"
            "  --validation       enable VK_LAYER_KHRONOS_validation\n"
            "  --gpu N            pick the Vulkan device by index (env SOUNDSCAPE_GPU); the log lists them\n"
            "  --render-scale X   supersample the scene X× the window size (default 2; 1 = off)\n"
            "  --no-maximize      start with a 1920×1080 window instead of maximized (1920×1080 is the minimum)\n"
            "  --ui-scale X       panel/HUD scale (default: auto from display scale and size)\n"
            "  --autoplay         press Play once the station is loaded\n"
            "  --screenshot PATH  write a PPM of the window after --screenshot-after seconds (default 8)\n"
            "  --exit-after SEC   quit after SEC seconds (after the screenshot, if any)\n"
            "keys: Space play/stop · N skip · S save · F/F11 fullscreen · Tab panel · H hud · C crt · Ctrl+Q quit");
}

int main(int argc, char** argv) {
  ss::Options o;
  if (const char* e = std::getenv("SOUNDSCAPE_API")) o.apiBase = e;
  if (const char* e = std::getenv("SOUNDSCAPE_GPU")) o.gpu = std::atoi(e);
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--api" && i + 1 < argc) o.apiBase = argv[++i];
    else if (a == "--station" && i + 1 < argc) o.station = argv[++i];
    else if (a == "--fullscreen") o.fullscreen = true;
    else if (a == "--no-vsync") o.vsync = false;
    else if (a == "--validation") o.validation = true;
    else if (a == "--autoplay") o.autoplay = true;
    else if (a == "--gpu" && i + 1 < argc) o.gpu = std::atoi(argv[++i]);
    else if (a == "--render-scale" && i + 1 < argc) o.renderScale = float(std::atof(argv[++i]));
    else if (a == "--no-maximize") o.maximized = false;
    else if (a == "--ui-scale" && i + 1 < argc) o.uiScale = float(std::atof(argv[++i]));
    else if (a == "--fullscreen-toggle-at" && i + 1 < argc) o.fullscreenToggleAt = std::atof(argv[++i]);
    else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
    else if (a == "--screenshot-after" && i + 1 < argc) o.screenshotAfter = std::atof(argv[++i]);
    else if (a == "--exit-after" && i + 1 < argc) o.exitAfter = std::atof(argv[++i]);
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
  }
  while (!o.apiBase.empty() && o.apiBase.back() == '/') o.apiBase.pop_back();
  ss::App app(o);
  return app.run();
}
