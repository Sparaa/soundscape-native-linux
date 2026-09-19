// Dear ImGui side panel (stations / radio / seeds / playlist / profile) in the web app's "terminal pane" look, and the
// red telemetry HUD over the pulse scene.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "app.hpp"

namespace ss {

namespace {
ImVec4 rgba(uint32_t hex, float a = 1.f) { return ImVec4(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a); }
const ImVec4 PH = rgba(0xd8dedc), PH2 = rgba(0x939a98), PH3 = rgba(0x5f6664), LINE = rgba(0x3b4140), BG = rgba(0x050606, 0.84f);
const ImVec4 RED = rgba(0xff2a3c), RED_DIM = rgba(0xa0141f), ERR = rgba(0xff6b6b);

std::string upper(std::string s) { for (auto& c : s) c = char(std::toupper(unsigned(c))); return s; }
std::string ellipsis(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "…"; }

void applyStyle(float scale) {
  ImGuiStyle& st = ImGui::GetStyle();
  st = ImGuiStyle();
  st.WindowRounding = 2; st.ChildRounding = 2; st.FrameRounding = 2; st.GrabRounding = 2; st.ScrollbarRounding = 2;
  st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.FrameBorderSize = 1; st.PopupBorderSize = 1;
  st.WindowPadding = ImVec2(10, 10); st.FramePadding = ImVec2(8, 5); st.ItemSpacing = ImVec2(8, 6); st.ItemInnerSpacing = ImVec2(6, 4);
  st.ScrollbarSize = 8; st.GrabMinSize = 10;
  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = PH; c[ImGuiCol_TextDisabled] = PH3; c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ChildBg] = BG; c[ImGuiCol_PopupBg] = rgba(0x050606, 0.96f);
  c[ImGuiCol_Border] = LINE; c[ImGuiCol_FrameBg] = rgba(0x0b0d0d, 0.9f); c[ImGuiCol_FrameBgHovered] = rgba(0x161919); c[ImGuiCol_FrameBgActive] = rgba(0x1e2222);
  c[ImGuiCol_Button] = rgba(0x0b0d0d, 0.9f); c[ImGuiCol_ButtonHovered] = rgba(0x1e2222); c[ImGuiCol_ButtonActive] = rgba(0x2a2f2f);
  c[ImGuiCol_Header] = rgba(0x161919); c[ImGuiCol_HeaderHovered] = rgba(0x1e2222); c[ImGuiCol_HeaderActive] = rgba(0x2a2f2f);
  c[ImGuiCol_SliderGrab] = PH; c[ImGuiCol_SliderGrabActive] = PH; c[ImGuiCol_CheckMark] = PH; c[ImGuiCol_PlotHistogram] = PH2;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = LINE; c[ImGuiCol_ScrollbarGrabHovered] = PH3; c[ImGuiCol_ScrollbarGrabActive] = PH2;
  c[ImGuiCol_Separator] = LINE; c[ImGuiCol_TextSelectedBg] = rgba(0x2a2f2f);
  st.ScaleAllSizes(scale);
}

/** A button that reads "active" (light on dark) when `on`. */
bool toggle(const char* label, bool on) {
  if (on) { ImGui::PushStyleColor(ImGuiCol_Button, PH); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, PH); ImGui::PushStyleColor(ImGuiCol_ButtonActive, PH2); ImGui::PushStyleColor(ImGuiCol_Text, rgba(0x050606)); }
  bool r = ImGui::Button(label);
  if (on) ImGui::PopStyleColor(4);
  return r;
}
void dim(const char* text) { ImGui::PushStyleColor(ImGuiCol_Text, PH2); ImGui::TextWrapped("%s", text); ImGui::PopStyleColor(); }
void dimUnwrapped(const std::string& text) { ImGui::PushStyleColor(ImGuiCol_Text, PH2); ImGui::TextUnformatted(text.c_str()); ImGui::PopStyleColor(); }

std::string statusLine(const App& a) {
  if (a.mode == "playlist") { char b[64]; std::snprintf(b, sizeof b, "playlist · %d saved%s", int(a.playlist.items.size()), a.paused ? " · paused" : ""); return b; }
  const RadioStatus& s = a.status; char b[160];
  if (s.state == "playing") { std::snprintf(b, sizeof b, "%s · %d cued", a.paused ? "paused" : "playing", int(s.ready.size())); return b; }
  if (s.state == "warming") {
    if (s.rendering) std::snprintf(b, sizeof b, "warming · %s %d%% · %d cued", s.rendering->stage.c_str(), int(std::lround(s.rendering->progress * 100)), int(s.ready.size()));
    else std::snprintf(b, sizeof b, "warming · %d cued", int(s.ready.size()));
    return b;
  }
  return s.state.empty() ? "stopped" : s.state;
}

// ------------------------------------------------------------------ HUD -----------------------------------------------
void glowText(const ImVec4& col, const std::string& s) {
  ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 halo = ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, 0.22f));
  for (auto [dx, dy] : { std::pair{ 1.f, 0.f }, { -1.f, 0.f }, { 0.f, 1.f }, { 0.f, -1.f } }) dl->AddText(ImVec2(p.x + dx, p.y + dy), halo, s.c_str());
  ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(s.c_str()); ImGui::PopStyleColor();
}

void drawHud(App& a, float sceneW, float sceneH) {
  static int asciiMeter = -1;
  if (asciiMeter < 0) asciiMeter = a.font && a.font->IsGlyphInFont(0x25B0) && a.font->IsGlyphInFont(0x25B1) ? 0 : 1;
  const bool ascii = asciiMeter == 1;
  const float s = a.uiScale;
  ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
  ImGui::SetNextWindowPos(ImVec2(20 * s, 14 * s));
  ImGui::Begin("hud-top", nullptr, fl);
  glowText(RED_DIM, "SOUNDSCAPE LINK PROTOCOL // SPECTRAL TELEMETRY");
  ImGui::PushFont(a.fontBold);
  glowText(RED, a.station ? "[ STATION :: " + upper(a.station->name) + " ]" : "[ SOUNDSCAPE ]");
  ImGui::PopFont();
  ImGui::End();
  const VisualFrame& f = a.frame; char b[256];
  ImGui::SetNextWindowPos(ImVec2(20 * s, sceneH - 16 * s), ImGuiCond_Always, ImVec2(0, 1));
  ImGui::Begin("hud-bottom", nullptr, fl);
  ImGui::PushFont(a.fontBold); glowText(RED, "── NOW PLAYING ── " + upper(a.song ? (a.song->title.empty() ? a.song->id : a.song->title) : "STANDBY")); ImGui::PopFont();
  std::snprintf(b, sizeof b, "BASS %s %3d%%", meter(f.bands.bass, 18, ascii).c_str(), int(std::lround(f.bands.bass * 100))); glowText(RED, b);
  std::snprintf(b, sizeof b, "MID  %s %3d%%", meter(f.bands.mid * 1.6f, 18, ascii).c_str(), int(std::lround(f.bands.mid * 100))); glowText(RED, b);
  std::snprintf(b, sizeof b, "TREB %s %3d%%", meter(f.bands.treble * 3.f, 18, ascii).c_str(), int(std::lround(f.bands.treble * 100))); glowText(RED, b);
  std::string sec = f.section ? upper(f.section->label) : "—", mode = a.song && a.song->plan ? " · MODE :: " + upper(a.song->plan->mode) : "";
  std::snprintf(b, sizeof b, "BPM %d · BAR %ld · BEAT %s · SECTION :: %s%s", int(std::lround(f.beat.bpm)), f.beat.bar, meter(float(1 - f.beat.phase), 4, ascii).c_str(), sec.c_str(), mode.c_str());
  glowText(RED_DIM, b);
  ImGui::End();
  // scene footer: fps · signal
  ImGui::SetNextWindowPos(ImVec2(sceneW - 12 * s, sceneH - 10 * s), ImGuiCond_Always, ImVec2(1, 1));
  ImGui::Begin("hud-foot", nullptr, fl);
  std::snprintf(b, sizeof b, "pulse · %s · %d fps · %.2f", a.vk.gpuName.c_str(), int(a.fps), f.bands.rms);
  ImGui::PushStyleColor(ImGuiCol_Text, PH3); ImGui::TextUnformatted(b); ImGui::PopStyleColor();
  ImGui::End();
}

// ------------------------------------------------------------------ panes ---------------------------------------------
void stationsPane(App& a) {
  ImGui::BeginChild("stations", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  ImGui::PushFont(a.fontBold); ImGui::TextUnformatted("STATIONS"); ImGui::PopFont();
  if (a.stations.empty()) dim("no stations yet — make one below, then add a seed (a file link or a YouTube link)");
  for (const auto& s : a.stations) {
    ImGui::PushID(s.id.c_str());
    char lbl[200]; std::snprintf(lbl, sizeof lbl, "%s", upper(ellipsis(s.name, 34)).c_str());
    if (ImGui::Selectable(lbl, false, 0, ImVec2(0, 0))) a.selectStation(s.id);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 90 * a.uiScale);
    char right[48]; std::snprintf(right, sizeof right, "%d seed%s", s.seed_count, s.seed_count == 1 ? "" : "s"); dimUnwrapped(right);
    if (!s.style.empty()) dim(ellipsis(s.style, 90).c_str());
    ImGui::PopID();
  }
  ImGui::Separator();
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120 * a.uiScale);
  bool enter = ImGui::InputTextWithHint("##newname", "station name", a.newStationName, sizeof a.newStationName, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("NEW STATION") || enter) a.createStation();
  ImGui::EndChild();
  ImGui::BeginChild("health", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  if (!a.healthKnown) { ImGui::PushStyleColor(ImGuiCol_Text, ERR); ImGui::TextWrapped("api offline at %s — is `docker compose up` running?", a.api.base().c_str()); ImGui::PopStyleColor(); }
  else {
    auto line = [&](const char* n, const SidecarHealth& h) { std::string t = std::string(n) + " · " + (h.ok ? (h.gpu.empty() ? "gpu?" : h.gpu) + std::string(" · ") + (h.loaded ? "loaded" : "idle") : "offline"); dimUnwrapped(t); };
    line("yue2", a.health.yue2); line("sheetsage", a.health.sheetsage); line("clipgrab", a.health.clipgrab);
    if (!a.health.llm_model.empty()) dimUnwrapped("writer · " + a.health.llm_model);
  }
  ImGui::EndChild();
}

void radioPane(App& a) {
  const float s = a.uiScale;
  ImGui::BeginChild("radio", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  bool playing = a.player && a.player->currentSong() && !a.paused;
  if (ImGui::Button(playing ? "■ STOP" : "▶ PLAY")) { if (playing) a.stopRadio(); else a.play(); }
  ImGui::SameLine(); if (ImGui::Button("» SKIP")) a.skip();
  ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 118 * s);
  if (toggle("NEW", a.mode == "radio")) a.setMode("radio");
  ImGui::SameLine(0, 2); if (toggle("SAVED", a.mode == "playlist")) a.setMode("playlist");
  dimUnwrapped(statusLine(a));
  if (a.mode == "radio") {
    ImGui::AlignTextToFramePadding(); dimUnwrapped("covers"); ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 34 * s);
    ImGui::SliderFloat("##covers", &a.covers, 0.f, 1.f, "");
    if (ImGui::IsItemDeactivatedAfterEdit()) a.commitCovers();
    ImGui::SameLine(); dimUnwrapped("new");
  }
  if (a.song) {
    const Song& sg = *a.song;
    ImGui::PushFont(a.fontBig); ImGui::TextWrapped("%s", upper(sg.title.empty() ? sg.id : sg.title).c_str()); ImGui::PopFont();
    std::string pl = planLabel(sg.plan);
    if (sg.plan) { char b[64]; std::snprintf(b, sizeof b, " · %d BPM", int(std::lround(sg.plan->bpm))); pl += b; if (!sg.plan->key.empty()) pl += " · " + sg.plan->key; }
    if (!pl.empty()) dim(pl.c_str());
    float frac = a.pos.d > 0 ? float(std::clamp(a.pos.t / a.pos.d, 0.0, 1.0)) : 0.f;
    ImGui::ProgressBar(frac, ImVec2(-1, 4 * s), "");
    dimUnwrapped(fmtTime(a.pos.t) + " / " + fmtTime(a.pos.d));
    if (toggle(sg.saved ? "SAVED" : "SAVE", sg.saved)) a.toggleSaved();
    ImGui::SameLine(); if (toggle("↑ MORE LIKE THIS", sg.vote > 0)) a.vote(1);
    ImGui::SameLine(); if (toggle("↓ LESS", sg.vote < 0)) a.vote(-1);
    if (!sg.lyrics.empty()) {
      ImGui::BeginChild("lyrics", ImVec2(0, 190 * s), ImGuiChildFlags_None);
      ImGui::PushStyleColor(ImGuiCol_Text, PH2); ImGui::TextWrapped("%s", sg.lyrics.c_str()); ImGui::PopStyleColor();
      ImGui::EndChild();
    }
  } else if (!a.notice.empty()) dim(a.notice.c_str());
  else dim(a.mode == "playlist" ? "press PLAY to hear the saved songs in order" : "press PLAY — the station cues two songs and keeps composing while you listen");
  if (a.mode == "radio") {
    ImGui::PushFont(a.fontBold); ImGui::TextUnformatted("UP NEXT"); ImGui::PopFont();
    if (a.status.ready.empty() && !a.status.rendering) dim("nothing cued yet");
    for (size_t i = 0; i < a.status.ready.size(); i++) {
      const Song& r = a.status.ready[i]; ImGui::PushID(int(i));
      std::string label = ellipsis(r.title.empty() ? r.id : r.title, 30);
      if (ImGui::Selectable(label.c_str())) a.playCued(r);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("play this one now");
      std::string sub = planLabel(r.plan); if (r.seconds > 0) sub += " · " + fmtTime(r.seconds);
      dim(sub.c_str());
      ImGui::PopID();
    }
    if (a.status.rendering) {
      char b[200]; std::snprintf(b, sizeof b, "rendering · %s · %s %d%%", planLabel(a.status.rendering->plan).c_str(), a.status.rendering->stage.c_str(), int(std::lround(a.status.rendering->progress * 100)));
      dim(b);
    }
  }
  if (!a.err.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ERR); ImGui::TextWrapped("%s", a.err.c_str()); ImGui::PopStyleColor(); }
  ImGui::EndChild();
}

void stationPane(App& a) {
  const float s = a.uiScale; Station& st = *a.station;
  ImGui::BeginChild("station", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  char t0[40], t1[40]; std::snprintf(t0, sizeof t0, "PLAYLIST · %d", int(a.playlist.items.size())); std::snprintf(t1, sizeof t1, "SEEDS · %d", int(st.seeds.size()));
  if (toggle(t0, a.tab == 0)) a.tab = 0;
  ImGui::SameLine(0, 2);
  if (toggle(t1, a.tab == 1)) a.tab = 1;
  ImGui::SameLine(0, 2);
  if (toggle("PROFILE", a.tab == 2)) a.tab = 2;
  if (a.tab == 1) {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 52 * s);
    bool enter = ImGui::InputTextWithHint("##seedurl", "paste a youtube / any link", a.seedUrl, sizeof a.seedUrl, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(); if (ImGui::Button("ADD") || enter) a.addSeed();
    if (!a.notice.empty() && a.notice.rfind("fetching", 0) == 0) dim(a.notice.c_str());
    if (st.seeds.empty()) dim("no seeds — the station learns its sound from what you add here (audio file URL or YouTube link)");
    for (const auto& sd : st.seeds) {
      ImGui::PushID(sd.id.c_str());
      ImGui::BeginChild("seed", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
      ImGui::PushFont(a.fontBold); ImGui::TextUnformatted(ellipsis(sd.title, 34).c_str()); ImGui::PopFont();
      ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 60 * s);
      if (ImGui::SmallButton("REMOVE")) a.removeSeed(sd.id);
      std::string meta; if (!sd.key.empty()) meta += sd.key; if (sd.bpm > 0) { char b[24]; std::snprintf(b, sizeof b, "%s%d BPM", meta.empty() ? "" : " · ", int(std::lround(sd.bpm))); meta += b; }
      if (sd.seconds > 0) meta += (meta.empty() ? "" : " · ") + fmtTime(sd.seconds);
      for (size_t i = 0; i < sd.sections.size() && i < 3; i++) meta += " · " + sd.sections[i];
      if (!meta.empty()) dim(meta.c_str());
      if (!sd.style_guess.empty()) dim(("sounds like: " + sd.style_guess).c_str());
      ImGui::EndChild();
      ImGui::PopID();
    }
  } else if (a.tab == 0) {
    if (a.playlist.items.empty()) dim("nothing saved yet — SAVE a song while it plays and it lands here");
    for (size_t i = 0; i < a.playlist.items.size(); i++) {
      const Song& sg = a.playlist.items[i]; ImGui::PushID(int(i));
      bool cur = a.mode == "playlist" && int(i) == a.plIndex;
      std::string label = (cur ? "▶ " : "  ") + ellipsis(sg.title.empty() ? sg.id : sg.title, 34);
      if (ImGui::Selectable(label.c_str(), cur)) { a.setMode("playlist"); a.playSaved(int(i)); }
      ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 40 * s); dimUnwrapped(fmtTime(sg.seconds));
      ImGui::PopID();
    }
    if (!a.playlist.items.empty()) dimUnwrapped("total " + fmtTime(a.playlist.seconds));
  } else {
    if (!st.style.empty()) { ImGui::PushFont(a.fontBold); ImGui::TextUnformatted("SOUND"); ImGui::PopFont(); dim(st.style.c_str()); }
    auto tags = [&](const char* n, const std::vector<std::string>& v) { if (v.empty()) return; std::string t = n; t += ": "; for (size_t i = 0; i < v.size(); i++) t += (i ? ", " : "") + v[i]; dim(t.c_str()); };
    tags("mood", st.tags.mood); tags("genre", st.tags.genre);
    if (!st.language.empty()) dim(("language: " + st.language).c_str());
    if (!st.blurb.empty()) { ImGui::PushFont(a.fontBold); ImGui::TextUnformatted("BLURB"); ImGui::PopFont(); dim(st.blurb.c_str()); }
    if (st.style.empty() && st.blurb.empty()) dim("the profile appears after the first seed is analysed");
  }
  ImGui::EndChild();
}

void settingsPane(App& a) {
  const float s = a.uiScale;
  ImGui::BeginChild("settings", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  ImGui::AlignTextToFramePadding(); dimUnwrapped("volume"); ImGui::SameLine(); ImGui::SetNextItemWidth(110 * s); ImGui::SliderFloat("##vol", &a.volume, 0.f, 1.f, "");
  ImGui::SameLine(); ImGui::Checkbox("CRT", &a.crt); ImGui::SameLine(); ImGui::Checkbox("HUD", &a.showHud);
  ImGui::SameLine(); if (ImGui::Button(a.fullscreen ? "⛶ window" : "⛶ full")) a.toggleFullscreen();
  ImGui::AlignTextToFramePadding(); dimUnwrapped("glow"); ImGui::SameLine(); ImGui::SetNextItemWidth(110 * s); ImGui::SliderFloat("##glow", &a.glow, 0.f, 1.f, "");
  ImGui::SameLine(); dimUnwrapped("render"); ImGui::SameLine();
  for (float v : { 1.f, 2.f, 3.f }) { char l[8]; std::snprintf(l, sizeof l, "%gx", v); if (toggle(l, std::fabs(a.renderScale - v) < 0.01f)) a.renderScale = v; ImGui::SameLine(0, 2); }
  ImGui::NewLine();
  ImGui::AlignTextToFramePadding(); dimUnwrapped("ui"); ImGui::SameLine();
  for (float v : { 1.f, 1.5f, 2.f, 2.5f, 3.f }) { char l[8]; std::snprintf(l, sizeof l, "%gx", v); if (toggle(l, std::fabs(a.uiScale - v) < 0.01f)) { a.uiScale = v; a.uiScaleDirty = true; } ImGui::SameLine(0, 2); }
  ImGui::NewLine();
  ImGui::SameLine(); ImGui::AlignTextToFramePadding(); dimUnwrapped("api"); ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##api", a.apiEdit, sizeof a.apiEdit, ImGuiInputTextFlags_EnterReturnsTrue)) { a.api.setBase(a.apiEdit); a.healthKnown = false; a.backToStations(); a.refreshHealth(); }
  dimUnwrapped("Space play/stop · N skip · S save · F fullscreen · Tab panel · H hud · C crt");
  ImGui::EndChild();
}
} // namespace

void drawUI(App& a) {
  ImGuiIO& io = ImGui::GetIO();
  if (a.uiScaleDirty) { applyStyle(a.uiScale); ImGui::GetStyle().FontScaleMain = a.uiScale / a.uiBaseScale; a.uiScaleDirty = false; }
  const float W = io.DisplaySize.x, H = io.DisplaySize.y, pw = a.panelVisible ? 420 * a.uiScale : 0;
  if (a.showHud) drawHud(a, W - pw, H);
  if (!a.panelVisible) return;
  ImGui::SetNextWindowPos(ImVec2(W - pw, 0)); ImGui::SetNextWindowSize(ImVec2(pw, H));
  ImGui::Begin("panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  // header pane
  ImGui::BeginChild("hdr", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  if (a.station) {
    if (ImGui::Button("← stations")) a.backToStations();
    ImGui::SameLine(); ImGui::PushFont(a.fontBig); ImGui::TextUnformatted(upper(ellipsis(a.station->name, 22)).c_str()); ImGui::PopFont();
  } else { ImGui::PushFont(a.fontBig); ImGui::TextUnformatted("SOUNDSCAPE"); ImGui::PopFont(); ImGui::SameLine(); dimUnwrapped(a.api.base()); }
  ImGui::EndChild();
  if (a.station) { radioPane(a); stationPane(a); }
  else stationsPane(a);
  settingsPane(a);
  ImGui::End();
}

} // namespace ss
