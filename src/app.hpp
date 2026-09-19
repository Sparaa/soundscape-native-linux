#pragma once
// Application state and glue: api polling, player callbacks, analyser → VisualFrame, window/input, main loop.
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <imgui.h>
#include "abc.hpp"
#include "analysis.hpp"
#include "api.hpp"
#include "async.hpp"
#include "audio.hpp"
#include "player.hpp"
#include "renderer.hpp"
#include "vk_context.hpp"

namespace ss {

struct Options { std::string apiBase = "http://127.0.0.1:3021"; bool validation = false; bool vsync = true; bool fullscreen = false; std::string station;
  bool autoplay = false; std::string screenshot; double screenshotAfter = 8; double exitAfter = 0; int gpu = -1; float renderScale = 2.f; bool maximized = true; float uiScale = 0.f; /* 0 = auto */ };

struct App {
  explicit App(Options o);
  int run();

  // ---- core
  Options opt; MainQueue mq; Api api; AudioEngine audio; std::unique_ptr<RadioPlayer> player; Analyser analyser{ 2048, 0.8f };
  GLFWwindow* window = nullptr; VkContext vk; Renderer renderer; ImFont *font = nullptr, *fontBold = nullptr, *fontBig = nullptr; float uiScale = 1.f, uiBaseScale = 1.f; bool uiScaleDirty = true;

  // ---- radio state (main thread)
  std::vector<Station> stations; std::optional<Station> station; RadioStatus status; std::optional<Song> song; RadioPlayer::Pos pos{ 0, 0 };
  bool paused = false; std::string mode = "radio"; Playlist playlist; int plIndex = -1;
  std::string err, notice; Health health; bool healthKnown = false; bool busyStation = false;
  char newStationName[128] = ""; char seedUrl[512] = ""; char apiEdit[256] = ""; float covers = 0.5f; bool coversDragging = false; int tab = 1;
  bool panelVisible = true, fullscreen = false, crt = true; float renderScale = 2.f, glow = 0.12f, volume = 1.f, fps = 0; bool showHud = true;
  int savedWinX = 100, savedWinY = 100, savedWinW = 1600, savedWinH = 1000;

  // worker-visible copies (the player's next-song callback runs off the main thread)
  std::mutex wmx; std::string wStationId; std::vector<Song> wPlaylist; int wPlIndex = -1; std::string wMode = "radio";

  // ---- visual
  BeatClock clock{ 120, 0 }; std::vector<SectionCue> cues; Palette palette; std::string cueSong; VisualFrame frame;
  std::vector<uint8_t> fftBytes; std::vector<float> tap; double lastStatusPoll = -10, lastStationsPoll = -10, lastHealth = -10;

  // ---- actions
  void refreshStations();
  void refreshHealth();
  void selectStation(const std::string& id);
  void backToStations();
  void createStation();
  void play();
  void stopRadio();
  void skip();
  void toggleSaved();
  void vote(int v);
  void playCued(const Song& s);
  void commitCovers();
  void addSeed();
  void removeSeed(const std::string& id);
  void loadPlaylist();
  void playSaved(int idx);
  void setMode(const std::string& m);
  void toggleFullscreen();
  void applySongFlags(const Song& u);

  // ---- loop pieces
  void pollStatus(double now);
  void computeFrame(double dt);
  void handleKeys();
  std::optional<Song> nextForWorker();       // runs on a worker thread
  bool writeCapture();                       // after endFrame: PPM to opt.screenshot
};

void drawUI(App& app);            // ui.cpp: side panel + HUD
std::string fmtTime(double s);

} // namespace ss
