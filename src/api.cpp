#include "api.hpp"
#include <curl/curl.h>
#include <stdexcept>

using json = nlohmann::json;
namespace ss {

namespace {
struct CurlGlobal { CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); } ~CurlGlobal() { curl_global_cleanup(); } } g_curl;
size_t writeCb(char* p, size_t s, size_t n, void* ud) { auto* v = static_cast<std::vector<uint8_t>*>(ud); v->insert(v->end(), p, p + s * n); return s * n; }
std::string str(const json& j, const char* k) { auto it = j.find(k); return it != j.end() && it->is_string() ? it->get<std::string>() : ""; }
double num(const json& j, const char* k, double d = 0) { auto it = j.find(k); return it != j.end() && it->is_number() ? it->get<double>() : d; }
bool boolean(const json& j, const char* k) { auto it = j.find(k); return it != j.end() && it->is_boolean() && it->get<bool>(); }
std::optional<Plan> parsePlan(const json& j) {
  if (!j.is_object()) return std::nullopt;
  Plan p; p.mode = str(j, "mode"); p.seed_title = str(j, "seed_title"); p.theme = str(j, "theme"); p.explain = str(j, "explain"); p.key = str(j, "key");
  p.bpm = float(num(j, "bpm", 120)); p.duration_s = num(j, "duration_s"); return p;
}
TagLabels parseTags(const json& tags) {          // {mood: [{label, p|weight}], genre: [...]}
  TagLabels t;
  if (!tags.is_object()) return t;
  for (auto* key : { "mood", "genre" }) {
    auto it = tags.find(key);
    if (it == tags.end() || !it->is_array()) continue;
    for (const auto& e : *it) if (e.is_object()) (std::string(key) == "mood" ? t.mood : t.genre).push_back(str(e, "label"));
  }
  return t;
}
} // namespace

std::string planLabel(const std::optional<Plan>& p) {
  if (!p) return "";
  if (!p->explain.empty()) return p->explain;
  if (p->mode == "inspired") return "new song in the station's sound";
  if (p->mode == "faithful") return "cover";
  if (p->mode == "reinterpret") return "reinterpretation";
  if (p->mode == "hook") return "hook";
  return p->mode;
}

Song Api::parseSong(const json& j) {
  Song s; s.id = str(j, "id"); s.station_id = str(j, "station_id"); s.title = str(j, "title"); s.lyrics = str(j, "lyrics"); s.abc = str(j, "abc");
  s.status = str(j, "status"); s.explain = str(j, "explain"); s.style = str(j, "style"); s.seconds = num(j, "seconds"); s.liked = boolean(j, "liked");
  s.saved = boolean(j, "saved"); s.vote = int(num(j, "vote")); s.created = num(j, "created");
  if (j.contains("plan")) s.plan = parsePlan(j["plan"]);
  if (j.contains("tags")) s.tags = parseTags(j["tags"]);
  return s;
}
RadioStatus Api::parseStatus(const json& j) {
  RadioStatus r; r.state = str(j, "state"); r.buffer_target = int(num(j, "buffer_target", 2));
  if (j.contains("now_playing") && j["now_playing"].is_object()) r.now_playing = parseSong(j["now_playing"]);
  if (j.contains("ready") && j["ready"].is_array()) for (const auto& s : j["ready"]) r.ready.push_back(parseSong(s));
  if (j.contains("recent") && j["recent"].is_array()) for (const auto& s : j["recent"]) r.recent.push_back(parseSong(s));
  if (j.contains("rendering") && j["rendering"].is_object()) {
    const auto& x = j["rendering"]; Rendering rd; rd.plan = parsePlan(x.value("plan", json::object())).value_or(Plan{});
    rd.stage = str(x, "stage"); rd.progress = float(num(x, "progress")); rd.seconds = num(x, "seconds"); rd.attempt = int(num(x, "attempt")); r.rendering = rd;
  }
  return r;
}
Station Api::parseStation(const json& j) {
  Station s; s.id = str(j, "id"); s.name = str(j, "name"); s.created = num(j, "created");
  if (j.contains("settings") && j["settings"].is_object()) {
    const auto& st = j["settings"]; s.language = str(st, "language"); s.blurb = str(st, "blurb");
    if (st.contains("covers") && st["covers"].is_number()) { s.covers = st["covers"].get<float>(); s.has_covers = true; }
  }
  if (j.contains("profile") && j["profile"].is_object()) {
    const auto& p = j["profile"]; s.style = str(p, "style"); s.seed_count = int(num(p, "seeds"));
    if (p.contains("tags")) s.tags = parseTags(p["tags"]);
  }
  if (j.contains("seeds") && j["seeds"].is_array()) for (const auto& e : j["seeds"]) {
    Seed sd; sd.id = str(e, "id"); sd.title = str(e, "title"); sd.source = str(e, "source"); sd.key = str(e, "key"); sd.style_guess = str(e, "style_guess");
    sd.seconds = num(e, "seconds"); sd.bpm = float(num(e, "bpm"));
    if (e.contains("sections") && e["sections"].is_array()) for (const auto& x : e["sections"]) if (x.is_string()) sd.sections.push_back(x.get<std::string>());
    s.seeds.push_back(sd);
  }
  if (!s.seed_count) s.seed_count = int(s.seeds.size());
  return s;
}
Playlist Api::parsePlaylist(const json& j) {
  Playlist p; p.id = str(j, "id"); p.name = str(j, "name"); p.seconds = num(j, "seconds");
  if (j.contains("items") && j["items"].is_array()) for (const auto& s : j["items"]) p.items.push_back(parseSong(s));
  return p;
}

std::vector<uint8_t> Api::raw(const std::string& path, long timeoutS) {
  CURL* c = curl_easy_init();
  if (!c) throw std::runtime_error("curl init failed");
  std::vector<uint8_t> out; std::string url = base_ + path; char err[CURL_ERROR_SIZE] = {0};
  curl_easy_setopt(c, CURLOPT_URL, url.c_str()); curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb); curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L); curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutS); curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  CURLcode rc = curl_easy_perform(c); long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); curl_easy_cleanup(c);
  if (rc != CURLE_OK) throw std::runtime_error(std::string("api unreachable: ") + (err[0] ? err : curl_easy_strerror(rc)));
  if (code >= 400) throw std::runtime_error(std::to_string(code) + " " + std::string(out.begin(), out.begin() + std::min<size_t>(out.size(), 300)));
  return out;
}

json Api::req(const char* method, const std::string& path, const std::optional<json>& body, const std::optional<std::string>& form, long timeoutS) {
  CURL* c = curl_easy_init();
  if (!c) throw std::runtime_error("curl init failed");
  std::vector<uint8_t> out; std::string url = base_ + path; char err[CURL_ERROR_SIZE] = {0}; std::string payload;
  curl_slist* hdr = nullptr;
  curl_easy_setopt(c, CURLOPT_URL, url.c_str()); curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb); curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L); curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutS); curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
  if (body) { payload = body->dump(); hdr = curl_slist_append(hdr, "content-type: application/json"); curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str()); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, long(payload.size())); }
  else if (form) { payload = *form; hdr = curl_slist_append(hdr, "content-type: application/x-www-form-urlencoded"); curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str()); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, long(payload.size())); }
  else if (std::string(method) != "GET") { curl_easy_setopt(c, CURLOPT_POSTFIELDS, ""); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, 0L); }
  hdr = curl_slist_append(hdr, "accept: application/json"); curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
  CURLcode rc = curl_easy_perform(c); long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); curl_easy_cleanup(c); curl_slist_free_all(hdr);
  if (rc != CURLE_OK) throw std::runtime_error(std::string("api unreachable: ") + (err[0] ? err : curl_easy_strerror(rc)));
  std::string text(out.begin(), out.end());
  if (code >= 400) throw std::runtime_error(std::to_string(code) + " " + text.substr(0, 300));
  if (text.empty()) return json::object();
  return json::parse(text, nullptr, true, true);
}

static std::string urlenc(const std::string& s) { char* e = curl_easy_escape(nullptr, s.c_str(), int(s.size())); std::string r = e ? e : ""; curl_free(e); return r; }

Health Api::health() {
  json j = req("GET", "/healthz", std::nullopt, std::nullopt, 5); Health h; h.ok = boolean(j, "ok");
  auto sc = [&](const char* k) { SidecarHealth s; if (!j.contains("sidecars") || !j["sidecars"].contains(k)) return s; const auto& x = j["sidecars"][k]; s.ok = boolean(x, "ok"); s.gpu = str(x, "gpu"); s.loaded = boolean(x, "loaded"); s.error = str(x, "error"); return s; };
  h.yue2 = sc("yue2"); h.sheetsage = sc("sheetsage"); h.clipgrab = sc("clipgrab");
  if (j.contains("llm") && j["llm"].is_object()) h.llm_model = str(j["llm"], "model");
  return h;
}
std::vector<Station> Api::listStations() { std::vector<Station> v; for (const auto& s : req("GET", "/stations")) v.push_back(parseStation(s)); return v; }
Station Api::getStation(const std::string& id) { return parseStation(req("GET", "/stations/" + id)); }
Station Api::createStation(const std::string& name, const std::string& language) { return parseStation(req("POST", "/stations", json{ {"name", name}, {"language", language} })); }
Station Api::addSeedUrl(const std::string& stationId, const std::string& url) { return parseStation(req("POST", "/stations/" + stationId + "/seeds", std::nullopt, "url=" + urlenc(url), 600)); }
Station Api::deleteSeed(const std::string& seedId) { return parseStation(req("DELETE", "/seeds/" + seedId)); }
RadioStatus Api::radioPlay(const std::string& id) { return parseStatus(req("POST", "/stations/" + id + "/play")); }
RadioStatus Api::radioStop(const std::string& id) { return parseStatus(req("POST", "/stations/" + id + "/stop")); }
RadioStatus Api::radioStatus(const std::string& id) { return parseStatus(req("GET", "/stations/" + id + "/radio", std::nullopt, std::nullopt, 10)); }
std::pair<std::optional<Song>, RadioStatus> Api::radioNext(const std::string& id, const std::string& songId) {
  json j = req("POST", "/stations/" + id + "/next" + (songId.empty() ? "" : "?song_id=" + urlenc(songId)));
  std::optional<Song> s; if (j.contains("song") && j["song"].is_object()) s = parseSong(j["song"]);
  return { s, parseStatus(j.value("status", json::object())) };
}
std::vector<Song> Api::stationSongs(const std::string& id, int limit) { std::vector<Song> v; for (const auto& s : req("GET", "/stations/" + id + "/songs?limit=" + std::to_string(limit))) v.push_back(parseSong(s)); return v; }
Song Api::patchSong(const std::string& id, const json& flags) { return parseSong(req("PATCH", "/songs/" + id, flags)); }
Station Api::patchSettings(const std::string& id, const json& s) { return parseStation(req("PATCH", "/stations/" + id + "/settings", s)); }
Playlist Api::stationPlaylist(const std::string& id) { return parsePlaylist(req("GET", "/stations/" + id + "/playlist")); }
std::vector<Song> Api::librarySongs(bool saved, bool liked) {
  std::string q; if (saved) q += "saved=true"; if (liked) q += std::string(q.empty() ? "" : "&") + "liked=true";
  std::vector<Song> v; for (const auto& s : req("GET", "/library" + (q.empty() ? "" : "?" + q))) v.push_back(parseSong(s)); return v;
}
std::vector<uint8_t> Api::songAudio(const std::string& id) { return raw("/songs/" + id + "/audio", 180); }

} // namespace ss
