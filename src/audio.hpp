#pragma once
// Two-deck audio engine on miniaudio: FLAC decoded from memory, per-deck linear gain ramps (the crossfade), a master
// volume and a mono tap of the mixed output for the analyser — the WebAudio graph of web/lib/player.ts.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ss {

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();
  bool init(std::string& err);
  void shutdown();
  int sampleRate() const { return sampleRate_; }
  bool ready() const { return ready_; }

  bool load(int deck, std::vector<uint8_t> bytes, std::string& err);
  void unload(int deck);
  void setGain(int deck, float target, double rampSeconds);
  void setPlaying(int deck, bool playing);
  bool playing(int deck);
  bool loaded(int deck);
  bool ended(int deck);
  double position(int deck);
  double duration(int deck);
  void setVolume(float v) { volume_ = v; }
  float volume() const { return volume_; }

  /** The most recent `n` mono samples of the mixed output (oldest first). */
  void tapLatest(float* out, int n);

  struct Deck;
  struct Impl;      // public so the C audio callback can reach it
private:
  std::unique_ptr<Impl> impl_;
  int sampleRate_ = 48000;
  bool ready_ = false;
  std::atomic<float> volume_{ 1.f };
};

} // namespace ss
