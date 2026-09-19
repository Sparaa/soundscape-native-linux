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
            "keys: Space play/stop · N skip · S save · F/F11 fullscreen · Tab panel · H hud · C crt · Ctrl+Q quit");
}

int main(int argc, char** argv) {
  ss::Options o;
  if (const char* e = std::getenv("SOUNDSCAPE_API")) o.apiBase = e;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--api" && i + 1 < argc) o.apiBase = argv[++i];
    else if (a == "--station" && i + 1 < argc) o.station = argv[++i];
    else if (a == "--fullscreen") o.fullscreen = true;
    else if (a == "--no-vsync") o.vsync = false;
    else if (a == "--validation") o.validation = true;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
  }
  while (!o.apiBase.empty() && o.apiBase.back() == '/') o.apiBase.pop_back();
  ss::App app(o);
  return app.run();
}
