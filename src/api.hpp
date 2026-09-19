#pragma once
// Blocking HTTP client for the soundscape api (FastAPI on :3021) — the routes web/lib/api.ts uses.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "analysis.hpp"

namespace ss {

struct Plan { std::string mode, seed_title, theme, explain, key; float bpm = 120; double duration_s = 0; };
struct Song {
  std::string id, station_id, title, lyrics, abc, status, explain, style;
  std::optional<Plan> plan; double seconds = 0; bool liked = false, saved = false; int vote = 0; double created = 0;
  TagLabels tags;
};
struct Rendering { Plan plan; std::string stage; float progress = 0; double seconds = 0; int attempt = 0; };
struct RadioStatus {
  std::string state = "stopped"; std::optional<Song> now_playing; std::vector<Song> ready; std::optional<Rendering> rendering;
  int buffer_target = 2; std::vector<Song> recent;
};
struct Seed { std::string id, title, source, key, style_guess; double seconds = 0; float bpm = 0; std::vector<std::string> sections; };
struct Station {
  std::string id, name, language, blurb, style; double created = 0; float covers = 0.5f; bool has_covers = false;
  std::vector<Seed> seeds; TagLabels tags; int seed_count = 0;
};
struct Playlist { std::string id, name; std::vector<Song> items; double seconds = 0; };
struct SidecarHealth { bool ok = false; std::string gpu; bool loaded = false; std::string error; };
struct Health { bool ok = false; SidecarHealth yue2, sheetsage, clipgrab; std::string llm_model; };

std::string planLabel(const std::optional<Plan>& p);

class Api {
public:
  explicit Api(std::string base) : base_(std::move(base)) {}
  const std::string& base() const { return base_; }
  void setBase(std::string b) { base_ = std::move(b); }

  Health health();
  std::vector<Station> listStations();
  Station getStation(const std::string& id);
  Station createStation(const std::string& name, const std::string& language = "English");
  Station addSeedUrl(const std::string& stationId, const std::string& url);
  Station deleteSeed(const std::string& seedId);
  RadioStatus radioPlay(const std::string& id);
  RadioStatus radioStop(const std::string& id);
  RadioStatus radioStatus(const std::string& id);
  /** Pop the next cued song (or a specific cued one). */
  std::pair<std::optional<Song>, RadioStatus> radioNext(const std::string& id, const std::string& songId = "");
  std::vector<Song> stationSongs(const std::string& id, int limit = 50);
  Song patchSong(const std::string& id, const nlohmann::json& flags);
  Station patchSettings(const std::string& id, const nlohmann::json& s);
  Playlist stationPlaylist(const std::string& id);
  std::vector<Song> librarySongs(bool saved = false, bool liked = false);
  /** Whole FLAC of a song in memory (the decoder reads from it). */
  std::vector<uint8_t> songAudio(const std::string& id);

  static Song parseSong(const nlohmann::json& j);
  static RadioStatus parseStatus(const nlohmann::json& j);
  static Station parseStation(const nlohmann::json& j);
  static Playlist parsePlaylist(const nlohmann::json& j);
private:
  nlohmann::json req(const char* method, const std::string& path, const std::optional<nlohmann::json>& body = std::nullopt,
                     const std::optional<std::string>& form = std::nullopt, long timeoutS = 30);
  std::vector<uint8_t> raw(const std::string& path, long timeoutS);
  std::string base_;
};

} // namespace ss
