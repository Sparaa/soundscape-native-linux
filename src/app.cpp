#include "app.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace ss {

std::string fmtTime(double s) {
  if (!(s > 0) || !std::isfinite(s)) return "0:00";
  int m = int(s) / 60, sec = int(s) % 60; char b[16]; std::snprintf(b, sizeof b, "%d:%02d", m, sec); return b;
}

App::App(Options o) : opt(std::move(o)), api(opt.apiBase) { std::snprintf(apiEdit, sizeof apiEdit, "%s", opt.apiBase.c_str()); renderScale = opt.renderScale; }

// ------------------------------------------------------------------ actions -------------------------------------------
void App::refreshStations() {
  async_call<std::vector<Station>>(mq, [this] { return api.listStations(); },
    [this](std::vector<Station> v) { stations = std::move(v); err.clear(); },
    [this](std::string e) { err = e; });
}
void App::refreshHealth() {
  async_call<Health>(mq, [this] { return api.health(); }, [this](Health h) { health = h; healthKnown = true; }, [this](std::string) { healthKnown = false; });
}
void App::selectStation(const std::string& id) {
  busyStation = true;
  async_call<Station>(mq, [this, id] { return api.getStation(id); },
    [this](Station s) {
      busyStation = false; station = s; covers = s.has_covers ? s.covers : 0.5f; err.clear(); tab = 1;
      { std::lock_guard<std::mutex> g(wmx); wStationId = s.id; }
      lastStatusPoll = -10; loadPlaylist();
      if (opt.autoplay) { opt.autoplay = false; play(); }
    },
    [this](std::string e) { busyStation = false; err = e; });
}
void App::backToStations() {
  if (player) { player->stop(); }
  if (station) { std::string sid = station->id; async_call<RadioStatus>(mq, [this, sid] { return api.radioStop(sid); }, [](RadioStatus) {}, [](std::string) {}); }
  station.reset(); song.reset(); status = RadioStatus{}; paused = false; mode = "radio"; playlist = Playlist{}; plIndex = -1;
  { std::lock_guard<std::mutex> g(wmx); wStationId.clear(); wPlaylist.clear(); wPlIndex = -1; wMode = "radio"; }
  refreshStations();
}
void App::createStation() {
  std::string name = newStationName; if (name.empty()) return;
  async_call<Station>(mq, [this, name] { return api.createStation(name); },
    [this](Station s) { newStationName[0] = 0; stations.push_back(s); selectStation(s.id); }, [this](std::string e) { err = e; });
}

std::optional<Song> App::nextForWorker() {
  std::string sid, m; std::vector<Song> items; int idx;
  { std::lock_guard<std::mutex> g(wmx); sid = wStationId; m = wMode; items = wPlaylist; idx = wPlIndex; }
  if (m == "playlist") {
    if (items.empty()) return std::nullopt;
    idx = (idx + 1) % int(items.size());
    { std::lock_guard<std::mutex> g(wmx); wPlIndex = idx; }
    mq.post([this, idx] { plIndex = idx; });
    return items[idx];
  }
  if (sid.empty()) return std::nullopt;
  auto [s, st] = api.radioNext(sid);
  mq.post([this, st] { status = st; });
  return s;
}

void App::play() {
  if (!station || !player) return;
  err.clear();
  std::string sid = station->id;
  if (mode == "playlist") {
    if (player->paused() && player->resume()) { paused = false; return; }
    playSaved(std::max(0, plIndex));
    return;
  }
  async_call<RadioStatus>(mq, [this, sid] { return api.radioPlay(sid); },
    [this, sid](RadioStatus st) {
      status = st;
      if (player->paused() && player->resume()) { paused = false; return; }
      if (player->currentSong()) { paused = false; return; }
      notice = "waiting for the first song…";
      // start as soon as the first song is cued (the spare makes this instant after the first session)
      async_call<std::pair<std::optional<Song>, RadioStatus>>(mq, [this, sid] { return api.radioNext(sid); },
        [this](std::pair<std::optional<Song>, RadioStatus> r) {
          status = r.second;
          if (r.first) { notice.clear(); paused = false; player->start(*r.first); }
          else lastStatusPoll = -10;          // the status poll keeps asking /next while warming
        },
        [this](std::string e) { err = e; });
    },
    [this](std::string e) { err = e; });
}
void App::stopRadio() {
  if (!station) return;
  if (player) player->pause();
  paused = true;
  std::string sid = station->id;
  async_call<RadioStatus>(mq, [this, sid] { return api.radioStop(sid); }, [this](RadioStatus st) { status = st; }, [this](std::string e) { err = e; });
}
void App::skip() { if (player) player->skip(); }
void App::applySongFlags(const Song& u) {
  if (song && song->id == u.id) { song->saved = u.saved; song->liked = u.liked; song->vote = u.vote; }
  for (auto& r : status.ready) if (r.id == u.id) { r.saved = u.saved; r.vote = u.vote; }
  if (u.saved || (playlist.items.end() != std::find_if(playlist.items.begin(), playlist.items.end(), [&](const Song& s) { return s.id == u.id; }))) loadPlaylist();
}
void App::toggleSaved() {
  if (!song) return;
  std::string id = song->id; bool v = !song->saved;
  async_call<Song>(mq, [this, id, v] { return api.patchSong(id, { { "saved", v } }); }, [this](Song u) { applySongFlags(u); }, [this](std::string e) { err = e; });
}
void App::vote(int v) {
  if (!song) return;
  std::string id = song->id; int nv = song->vote == v ? 0 : v;
  async_call<Song>(mq, [this, id, nv] { return api.patchSong(id, { { "vote", nv } }); }, [this, nv](Song u) { applySongFlags(u); notice = nv > 0 ? "more like this — noted" : nv < 0 ? "less of this — noted" : ""; }, [this](std::string e) { err = e; });
}
void App::playCued(const Song& s) {
  if (!station || !player) return;
  std::string sid = station->id, id = s.id;
  if (paused || (status.state != "playing" && status.state != "warming")) {
    async_call<RadioStatus>(mq, [this, sid] { return api.radioPlay(sid); }, [this](RadioStatus st) { status = st; paused = false; }, [this](std::string e) { err = e; });
  }
  player->playNow([this, sid, id]() -> std::optional<Song> { auto [song, st] = api.radioNext(sid, id); mq.post([this, st] { status = st; }); return song; });
}
void App::commitCovers() {
  if (!station) return;
  std::string sid = station->id; float v = covers;
  async_call<Station>(mq, [this, sid, v] { return api.patchSettings(sid, { { "covers", v } }); }, [this](Station s) { if (station && station->id == s.id) { station->covers = s.covers; station->blurb = s.blurb; } }, [this](std::string e) { err = e; });
}
void App::addSeed() {
  if (!station) return;
  std::string url = seedUrl; if (url.empty()) return;
  std::string sid = station->id; notice = "fetching + analysing the seed (this can take a minute)…"; seedUrl[0] = 0;
  async_call<Station>(mq, [this, sid, url] { return api.addSeedUrl(sid, url); },
    [this](Station s) { notice.clear(); if (station && station->id == s.id) station = s; }, [this](std::string e) { notice.clear(); err = e; });
}
void App::removeSeed(const std::string& id) {
  async_call<Station>(mq, [this, id] { return api.deleteSeed(id); }, [this](Station s) { if (station && station->id == s.id) station = s; }, [this](std::string e) { err = e; });
}
void App::loadPlaylist() {
  if (!station) return;
  std::string sid = station->id;
  async_call<Playlist>(mq, [this, sid] { return api.stationPlaylist(sid); },
    [this](Playlist p) { playlist = p; std::lock_guard<std::mutex> g(wmx); wPlaylist = p.items; }, [](std::string) {});
}
void App::playSaved(int idx) {
  if (!player || playlist.items.empty()) return;
  idx = std::clamp(idx, 0, int(playlist.items.size()) - 1);
  plIndex = idx; { std::lock_guard<std::mutex> g(wmx); wPlIndex = idx; }
  Song s = playlist.items[idx];
  if (!player->currentSong()) { paused = false; player->start(s); return; }
  player->playNow([s]() -> std::optional<Song> { return s; });
  paused = false;
}
void App::setMode(const std::string& m) {
  mode = m; { std::lock_guard<std::mutex> g(wmx); wMode = m; }
  if (m == "playlist") { tab = 0; }
  else if (station) { std::string sid = station->id; async_call<RadioStatus>(mq, [this, sid] { return api.radioPlay(sid); }, [this](RadioStatus st) { status = st; }, [this](std::string e) { err = e; }); }
}
void App::toggleFullscreen() {
  fullscreen = !fullscreen;
  if (fullscreen) {
    savedMaximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
    if (savedMaximized) glfwRestoreWindow(window);                       // remember the un-maximized geometry
#ifdef GLFW_PLATFORM_WAYLAND
    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)                     // Wayland has no window positions
#endif
    glfwGetWindowPos(window, &savedWinX, &savedWinY);
    glfwGetWindowSize(window, &savedWinW, &savedWinH);
    GLFWmonitor* mon = glfwGetPrimaryMonitor(); const GLFWvidmode* vm = glfwGetVideoMode(mon);
    glfwSetWindowMonitor(window, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
    panelVisible = false;
  } else {
    glfwSetWindowMonitor(window, nullptr, savedWinX, savedWinY, std::max(1920, savedWinW), std::max(1080, savedWinH), 0);
    if (savedMaximized) glfwMaximizeWindow(window);
    panelVisible = true;
  }
}

// ------------------------------------------------------------------ loop pieces ---------------------------------------
void App::pollStatus(double now) {
  if (now - lastHealth > 15) { lastHealth = now; refreshHealth(); }
  if (!station) { if (now - lastStationsPoll > 5) { lastStationsPoll = now; refreshStations(); } return; }
  if (now - lastStatusPoll < 2) return;
  lastStatusPoll = now;
  std::string sid = station->id;
  bool wantAutoRestart = mode == "radio" && player && player->currentSong() && !paused;
  bool warmingStart = !notice.empty() && !player->currentSong() && mode == "radio";
  async_call<RadioStatus>(mq, [this, sid] { return api.radioStatus(sid); },
    [this, sid, wantAutoRestart, warmingStart](RadioStatus st) {
      status = st; if (!err.empty() && err.rfind("api unreachable", 0) == 0) err.clear();
      if (st.state == "stopped" && wantAutoRestart)
        async_call<RadioStatus>(mq, [this, sid] { return api.radioPlay(sid); }, [this](RadioStatus s2) { status = s2; }, [](std::string) {});
      if (warmingStart && !player->fetching() && (st.state == "playing" || st.state == "warming") && !st.ready.empty())
        async_call<std::pair<std::optional<Song>, RadioStatus>>(mq, [this, sid] { return api.radioNext(sid); },
          [this](std::pair<std::optional<Song>, RadioStatus> r) { status = r.second; if (r.first && !player->currentSong()) { notice.clear(); paused = false; player->start(*r.first); } }, [](std::string) {});
      if (warmingStart && st.state == "stopped") notice.clear();
    },
    [this](std::string e) { err = e; });
}

void App::computeFrame(double dt) {
  const int n = analyser.fftSize();
  tap.resize(n);
  audio.tapLatest(tap.data(), n);
  analyser.analyse(tap.data(), fftBytes);
  const float sr = float(audio.sampleRate());
  Bands bands = bandEnergies(fftBytes, sr, n);
  pos = player ? player->position() : RadioPlayer::Pos{ 0, 0 };
  if (song && song->id != cueSong) {
    cueSong = song->id;
    cues = song->abc.empty() ? std::vector<SectionCue>{} : sectionCues(song->abc, song->seconds);
    clock = BeatClock(song->plan ? song->plan->bpm : 120.f, pos.t);
    palette = paletteFor(&song->tags, song->id.empty() ? 0 : int(song->id[0]));
  } else if (!song && !cueSong.empty()) { cueSong.clear(); cues.clear(); }
  clock.update(pos.t, bands.bass);
  std::vector<float> spectrum = logSpectrum(fftBytes, sr, n, renderer.bins());
  float punch = punchDet.update(spectrum);
  frame = makeFrame(pos.t, bands, clock, cues, palette, pos.d, std::move(spectrum), punch);
}

void App::handleKeys() {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) return;
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) { if (player && player->currentSong() && !paused) stopRadio(); else play(); }
  if (ImGui::IsKeyPressed(ImGuiKey_N, false)) skip();
  if (ImGui::IsKeyPressed(ImGuiKey_S, false)) toggleSaved();
  if (ImGui::IsKeyPressed(ImGuiKey_F, false) || ImGui::IsKeyPressed(ImGuiKey_F11, false)) toggleFullscreen();
  if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) panelVisible = !panelVisible;
  if (ImGui::IsKeyPressed(ImGuiKey_H, false)) showHud = !showHud;
  if (ImGui::IsKeyPressed(ImGuiKey_C, false)) crt = !crt;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && fullscreen) toggleFullscreen();
  if (ImGui::IsKeyPressed(ImGuiKey_Q, false) && io.KeyCtrl) glfwSetWindowShouldClose(window, 1);
}

bool App::writeCapture() {
  std::vector<uint8_t> px; uint32_t w = 0, h = 0; bool bgr = false;
  if (!vk.takeCapture(px, w, h, bgr)) return false;
  FILE* f = std::fopen(opt.screenshot.c_str(), "wb");
  if (!f) { std::fprintf(stderr, "[soundscape] cannot write %s\n", opt.screenshot.c_str()); return true; }
  std::fprintf(f, "P6\n%u %u\n255\n", w, h);
  std::vector<uint8_t> row(size_t(w) * 3);
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t* src = px.data() + size_t(y) * w * 4;
    for (uint32_t x = 0; x < w; x++) { row[x * 3] = src[x * 4 + (bgr ? 2 : 0)]; row[x * 3 + 1] = src[x * 4 + 1]; row[x * 3 + 2] = src[x * 4 + (bgr ? 0 : 2)]; }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
  std::fprintf(stderr, "[soundscape] screenshot %ux%u → %s\n", w, h, opt.screenshot.c_str());
  return true;
}

// ------------------------------------------------------------------ run -----------------------------------------------
static void glfwErr(int code, const char* d) { std::fprintf(stderr, "[glfw] %d: %s\n", code, d); }

int App::run() {
  glfwSetErrorCallback(glfwErr);
#ifdef GLFW_PLATFORM_WAYLAND
  // Native Wayland when the session offers it (GLFW 3.4): the display GPU can present directly. SOUNDSCAPE_X11=1 forces Xwayland.
  if (std::getenv("WAYLAND_DISPLAY") && !std::getenv("SOUNDSCAPE_X11") && glfwPlatformSupported(GLFW_PLATFORM_WAYLAND)) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif
  if (!glfwInit()) {
#ifdef GLFW_PLATFORM_WAYLAND
    glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
    if (!glfwInit())
#endif
    { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
  }
  if (!glfwVulkanSupported()) { std::fprintf(stderr, "No Vulkan loader/ICD found (install libvulkan1 and your GPU's Vulkan driver)\n"); return 1; }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  if (opt.maximized && !opt.fullscreen) glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
  window = glfwCreateWindow(savedWinW, savedWinH, "Soundscape", nullptr, nullptr);
  if (!window) { std::fprintf(stderr, "window creation failed\n"); return 1; }
  glfwSetWindowSizeLimits(window, 1920, 1080, GLFW_DONT_CARE, GLFW_DONT_CARE);   // the layout is designed for 1080p and up
  // UI scale: the compositor's content scale × a size factor, so a 4K desktop at 100 % still gets readable panes.
  float sx = 1, sy = 1; glfwGetWindowContentScale(window, &sx, &sy);
  int fbw = 0, fbh = 0; glfwGetFramebufferSize(window, &fbw, &fbh);
  uiScale = opt.uiScale > 0 ? opt.uiScale : std::max(1.f, sx) * std::clamp(fbh / (1100.f * std::max(1.f, sx)), 1.f, 2.5f);
  uiScale = std::round(uiScale * 4) / 4;
  uiBaseScale = uiScale;
  try {
    vk.vsync = opt.vsync; vk.preferredGpu = opt.gpu;
    vk.init(window, opt.validation);
    renderer.init(vk);
  } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
  const char* platform = "x11";
#ifdef GLFW_PLATFORM_WAYLAND
  platform = glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? "wayland" : glfwGetPlatform() == GLFW_PLATFORM_X11 ? "x11" : "other";
#endif
  std::fprintf(stderr, "[soundscape] GPU: %s · %s · api %s · ui %.2fx · render %.1fx\n", vk.gpuName.c_str(), platform, api.base().c_str(), uiScale, renderScale);
  for (size_t i = 0; i < vk.gpuNames.size(); i++) std::fprintf(stderr, "[soundscape]   --gpu %zu = %s\n", i, vk.gpuNames[i].c_str());
  // ---- ImGui
  IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
  const char* mono = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", *monoB = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf";
  font = io.Fonts->AddFontFromFileTTF(mono, 13.f * uiScale); if (!font) font = io.Fonts->AddFontDefault();
  fontBold = io.Fonts->AddFontFromFileTTF(monoB, 13.f * uiScale); if (!fontBold) fontBold = font;
  fontBig = io.Fonts->AddFontFromFileTTF(monoB, 18.f * uiScale); if (!fontBig) fontBig = fontBold;
  ImGui_ImplGlfw_InitForVulkan(window, true);
  ImGui_ImplVulkan_InitInfo ii{}; ii.ApiVersion = vk.apiVersion; ii.Instance = vk.instance; ii.PhysicalDevice = vk.phys; ii.Device = vk.device; ii.QueueFamily = vk.qfam; ii.Queue = vk.queue;
  ii.DescriptorPoolSize = 16; ii.MinImageCount = vk.minImageCount; ii.ImageCount = uint32_t(vk.scImages.size()); ii.PipelineInfoMain.RenderPass = vk.scPass; ii.PipelineInfoMain.Subpass = 0; ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ii.CheckVkResultFn = [](VkResult r) { if (r != VK_SUCCESS) std::fprintf(stderr, "[imgui vulkan] error %d\n", int(r)); };
  ImGui_ImplVulkan_Init(&ii);
  vk.onSwapchainRecreated = [this] { ImGui_ImplVulkan_SetMinImageCount(vk.minImageCount); };
  // ---- audio + player
  std::string aerr;
  if (!audio.init(aerr)) err = "audio: " + aerr;
  player = std::make_unique<RadioPlayer>(audio, api, mq);
  player->onSongChange = [this](const std::optional<Song>& s) { song = s; if (s) notice.clear(); };
  player->onNextError = [this](const std::string& e) { err = "next song: " + e.substr(0, 80) + " — retrying"; };
  player->onNeedNext = [this]() -> std::optional<Song> { return nextForWorker(); };
  refreshStations(); refreshHealth();
  if (!opt.station.empty()) selectStation(opt.station);
  if (opt.fullscreen) toggleFullscreen();
  // ---- loop
  double last = glfwGetTime(), fpsT = last, t0 = last; int frames = 0; long totalFrames = 0; bool shotRequested = false, shotDone = false;
  const bool profile = std::getenv("SOUNDSCAPE_PROFILE") != nullptr; double prof[6] = {}; const char* profName[6] = { "events+tick", "analyse", "acquire", "imgui", "record", "submit+present" };
  auto mark = [&](int k, double& tm) { if (!profile) return; double n = glfwGetTime(); prof[k] += n - tm; tm = n; };
  long punchHigh = 0, punchPeaks = 0, hitPeaks = 0, playFrames = 0; float lastPunch = 0, lastHit = 0;   // profile: does the pulse engage?
  while (!glfwWindowShouldClose(window)) {
    double tm = glfwGetTime();
    glfwPollEvents();
    mq.drain();
    double now = glfwGetTime(); float dt = float(std::min(0.1, now - last)); last = now;
    player->tick(now);
    pollStatus(now);
    audio.setVolume(volume);
    if (opt.fullscreenToggleAt > 0 && now - t0 >= opt.fullscreenToggleAt) { opt.fullscreenToggleAt += (fullscreen ? 1e9 : 3); toggleFullscreen(); }
    mark(0, tm);
    computeFrame(dt);
    if (profile && song) { playFrames++; if (frame.beat.punch > 0.5f) punchHigh++; if (frame.beat.punch > 0.6f && lastPunch <= 0.6f) punchPeaks++; if (frame.beat.hit > 0.9f && lastHit <= 0.9f) hitPeaks++; lastPunch = frame.beat.punch; lastHit = frame.beat.hit; }
    mark(1, tm);
    uint32_t imageIndex = 0; VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!vk.beginFrame(imageIndex, cmd)) continue;
    mark(2, tm);
    if (!opt.screenshot.empty() && !shotRequested && now - t0 >= opt.screenshotAfter) { shotRequested = true; vk.requestCapture(); }
    ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    handleKeys();
    drawUI(*this);
    ImGui::Render();
    mark(3, tm);
    RenderParams rp; rp.x = 0; rp.y = 0; rp.h = int(vk.extent.height);
    const int panelW = panelVisible ? int(420 * uiScale) : 0;
    rp.w = std::max(1, int(vk.extent.width) - panelW); rp.crt = crt; rp.glow = glow; rp.time = now; rp.renderScale = renderScale;
    renderer.render(cmd, imageIndex, frame, dt, rp);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    mark(4, tm);
    vk.endFrame(imageIndex);
    mark(5, tm);
    if (shotRequested && !shotDone && writeCapture()) shotDone = true;
    if (opt.exitAfter > 0 && now - t0 >= opt.exitAfter && (opt.screenshot.empty() || shotDone)) glfwSetWindowShouldClose(window, 1);
    frames++; totalFrames++; if (now - fpsT > 1) { fps = float(frames / (now - fpsT)); frames = 0; fpsT = now; }
  }
  vkDeviceWaitIdle(vk.device);
  if (profile && totalFrames) for (int k = 0; k < 6; k++) std::fprintf(stderr, "[profile] %-15s %6.2f ms/frame\n", profName[k], prof[k] * 1000 / totalFrames);
  if (profile && playFrames) std::fprintf(stderr, "[profile] pulse: %.1f s of playback · punch peaks %.2f/s · punch > 0.5 for %.0f%% of frames · bass-hit peaks %.2f/s\n",
                                          playFrames / 60.0, punchPeaks / (playFrames / 60.0), 100.0 * punchHigh / playFrames, hitPeaks / (playFrames / 60.0));
  std::fprintf(stderr, "[soundscape] %ld frames in %.1f s (%.0f fps avg, %s)\n", totalFrames, glfwGetTime() - t0, totalFrames / std::max(1e-3, glfwGetTime() - t0), opt.vsync ? "fifo" : "mailbox/immediate");
  if (station && player && player->currentSong()) { try { api.radioStop(station->id); } catch (...) {} }
  player.reset(); audio.shutdown();
  ImGui_ImplVulkan_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
  renderer.destroy(); vk.destroy();
  glfwDestroyWindow(window); glfwTerminate();
  return 0;
}

} // namespace ss
