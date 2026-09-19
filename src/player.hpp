#pragma once
// Radio player core — a port of web/lib/player.ts: two decks with an equal-time crossfade, pulls the next song from
// the api near the end of the current one, a stuck watchdog, and a fetch that never stays stuck.
#include <functional>
#include <optional>
#include <vector>
#include "api.hpp"
#include "async.hpp"
#include "audio.hpp"

namespace ss {

constexpr double CROSSFADE_S = 3;
constexpr double STUCK_S = 15;

double nextStartAt(double durationS, double crossfadeS = CROSSFADE_S);

class RadioPlayer {
public:
  /** Resolves the next song on a worker thread (blocking call allowed); nullopt = nothing cued yet. */
  using NextFn = std::function<std::optional<Song>()>;

  RadioPlayer(AudioEngine& audio, Api& api, MainQueue& mq);
  std::function<void(const std::optional<Song>&)> onSongChange = [](auto&) {};
  NextFn onNeedNext = []() -> std::optional<Song> { return std::nullopt; };
  std::function<void(const std::string&)> onNextError = [](auto&) {};

  void start(const Song& first);                 // downloads, then plays on the current deck
  void tick(double now);                         // every frame; the 250 ms cadence is internal
  void skip();
  bool playNow(NextFn get, double seconds = 0.5);
  void pause();
  bool paused();
  bool resume();
  void stop();
  struct Pos { double t, d; };
  Pos position();
  std::optional<Song> currentSong() const { return decks_[active_].song; }
  bool fetching() const { return fetching_; }
  bool playingIntent() const { return playing_; }

private:
  struct Deck { std::optional<Song> song; int gen = 0; };
  AudioEngine& audio_; Api& api_; MainQueue& mq_;
  Deck decks_[2]; int active_ = 0;
  bool ticking_ = false, playing_ = false, fetching_ = false;
  double nextTick_ = 0, retryAt_ = 0, lastT_ = -1, lastMove_ = 0, now_ = 0;
  std::vector<std::pair<double, std::function<void()>>> timers_;

  Deck& current() { return decks_[active_]; }
  Deck& standby() { return decks_[1 - active_]; }
  int idx(const Deck& d) const { return &d == &decks_[0] ? 0 : 1; }
  void pull(double crossfadeS, NextFn get);
  void crossfadeTo(const Song& next, std::vector<uint8_t> bytes, double seconds);
  void release(int deckIdx);
  void after(double seconds, std::function<void()> fn) { timers_.push_back({ now_ + seconds, std::move(fn) }); }
};

} // namespace ss
