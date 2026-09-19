#include "player.hpp"
#include <cmath>
#include <limits>

namespace ss {

double nextStartAt(double durationS, double crossfadeS) {
  if (!(durationS > 0) || !std::isfinite(durationS)) return std::numeric_limits<double>::infinity();
  return std::max(1.0, durationS - crossfadeS);
}

RadioPlayer::RadioPlayer(AudioEngine& audio, Api& api, MainQueue& mq) : audio_(audio), api_(api), mq_(mq) {}

void RadioPlayer::start(const Song& first) {
  if (fetching_) return;
  fetching_ = true;
  std::string id = first.id;
  async_call<std::vector<uint8_t>>(mq_, [this, id] { return api_.songAudio(id); },
    [this, first](std::vector<uint8_t> bytes) {
      fetching_ = false;
      std::string err;
      Deck& cur = current();
      cur.gen++;
      if (!audio_.load(idx(cur), std::move(bytes), err)) { onNextError("\"" + first.title + "\": " + err); return; }
      cur.song = first;
      audio_.setGain(idx(cur), 1.f, 0);
      audio_.setPlaying(idx(cur), true);
      playing_ = true; ticking_ = true;
      lastT_ = -1; lastMove_ = now_;
      onSongChange(first);
    },
    [this](std::string e) { fetching_ = false; onNextError(e); });
}

void RadioPlayer::tick(double now) {
  now_ = now;
  for (size_t i = 0; i < timers_.size();) {
    if (timers_[i].first <= now) { auto fn = std::move(timers_[i].second); timers_.erase(timers_.begin() + long(i)); fn(); } else i++;
  }
  if (!ticking_ || now < nextTick_) return;
  nextTick_ = now + 0.25;
  Deck& cur = current();
  int ci = idx(cur);
  double d = audio_.duration(ci);
  double dur = d > 0 ? d : (cur.song ? cur.song->seconds : 0);
  double t = audio_.position(ci);
  if (t != lastT_ || !playing_ || fetching_) { lastT_ = t; lastMove_ = now; }
  bool stuck = playing_ && cur.song && audio_.playing(ci) && now - lastMove_ > STUCK_S;
  bool over = !cur.song || audio_.ended(ci) || stuck;
  bool due = playing_ && (over || t >= nextStartAt(dur));
  if (stuck) onNextError("\"" + (cur.song ? cur.song->title : std::string("?")) + "\" stopped moving — skipping ahead");
  if (due && !fetching_ && !standby().song && now >= retryAt_) pull(over ? 0.2 : CROSSFADE_S, onNeedNext);
}

void RadioPlayer::pull(double crossfadeS, NextFn get) {
  fetching_ = true;
  struct Result { std::optional<Song> song; std::vector<uint8_t> bytes; };
  async_call<Result>(mq_, [this, get] {
      Result r; r.song = get();
      if (r.song) r.bytes = api_.songAudio(r.song->id);
      return r;
    },
    [this, crossfadeS](Result r) {
      if (r.song) crossfadeTo(*r.song, std::move(r.bytes), crossfadeS);
      else retryAt_ = now_ + 1.0;
      fetching_ = false;
    },
    [this](std::string e) { retryAt_ = now_ + 1.0; onNextError(e); fetching_ = false; });
}

void RadioPlayer::crossfadeTo(const Song& next, std::vector<uint8_t> bytes, double seconds) {
  Deck& out = current(); Deck& inn = standby();
  int oi = idx(out), ii = idx(inn);
  std::string err;
  if (!audio_.load(ii, std::move(bytes), err)) { onNextError("\"" + next.title + "\": " + err); retryAt_ = now_ + 1.0; return; }
  inn.gen++; inn.song = next;
  int outGen = out.gen;
  playing_ = true; ticking_ = true;
  lastT_ = -1; lastMove_ = now_;
  audio_.setGain(ii, 0.f, 0); audio_.setPlaying(ii, true);
  audio_.setGain(oi, 0.f, seconds); audio_.setGain(ii, 1.f, seconds);
  active_ = 1 - active_;
  onSongChange(next);
  after(seconds + 0.1, [this, oi, outGen] {
    if (decks_[oi].gen != outGen || oi == active_) return;   // a later crossfade reused this deck: it owns it now
    release(oi);
  });
}

void RadioPlayer::release(int i) { audio_.unload(i); decks_[i].song.reset(); }

void RadioPlayer::skip() { if (fetching_) return; pull(0.5, onNeedNext); }
bool RadioPlayer::playNow(NextFn get, double seconds) { if (fetching_) return false; pull(seconds, std::move(get)); return true; }

void RadioPlayer::pause() { playing_ = false; ticking_ = false; for (int i = 0; i < 2; i++) audio_.setPlaying(i, false); }
bool RadioPlayer::paused() { return current().song.has_value() && !audio_.playing(idx(current())); }
bool RadioPlayer::resume() {
  if (!current().song) return false;
  playing_ = true; ticking_ = true; lastT_ = -1; lastMove_ = now_;
  audio_.setPlaying(idx(current()), true);
  return true;
}
void RadioPlayer::stop() {
  playing_ = false; ticking_ = false; timers_.clear();
  for (int i = 0; i < 2; i++) { decks_[i].gen++; audio_.unload(i); decks_[i].song.reset(); }
  onSongChange(std::nullopt);
}

RadioPlayer::Pos RadioPlayer::position() {
  int ci = idx(current());
  double d = audio_.duration(ci);
  return { audio_.position(ci), d > 0 ? d : (current().song ? current().song->seconds : 0) };
}

} // namespace ss
