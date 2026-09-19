#pragma once
// Visualizer inputs, pure and testable — a port of soundscape web/lib/visual.ts plus an emulation of the WebAudio
// AnalyserNode (Blackman window, 1/N magnitude, 0.8 smoothing, -100..-30 dB → bytes) so levels match the web app.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ss {

struct Bands { float bass = 0, mid = 0, treble = 0, rms = 0; };

/** Emulates AnalyserNode.getByteFrequencyData for a mono float stream. */
class Analyser {
public:
  explicit Analyser(int fftSize = 2048, float smoothing = 0.8f, float minDb = -100.f, float maxDb = -30.f);
  int fftSize() const { return n_; }
  int binCount() const { return n_ / 2; }
  /** `samples` = the most recent fftSize mono samples (older first). Fills `out` (binCount bytes). */
  void analyse(const float* samples, std::vector<uint8_t>& out);
private:
  int n_; float smoothing_, minDb_, maxDb_;
  std::vector<float> window_, re_, im_, mag_;
};

Bands bandEnergies(const std::vector<uint8_t>& fft, float sampleRate, int fftSize);
std::vector<float> logSpectrum(const std::vector<uint8_t>& fft, float sampleRate, int fftSize, int n = 64, float lo = 30, float hi = 9000);

/** Beat clock: phase runs at the planned BPM; bass onsets pull the phase toward the hit (PLL gain `lock`). */
class BeatClock {
public:
  float bpm; float lock, threshold; float hit = 0;
  explicit BeatClock(float bpm = 120, double startT = 0, float lock = 0.15f, float threshold = 0.12f);
  double period() const { return 60.0 / bpm; }
  double phase(double t) const;
  long beatIndex(double t) const;
  long bar(double t) const { return beatIndex(t) >= 0 ? beatIndex(t) / 4 : -((-beatIndex(t) + 3) / 4); }
  bool update(double t, float bass);
private:
  double t0_; float lastBass_ = 0; double lastHitT_ = -1;
};

/** Percussive transient envelope ("punch") — port of visual.ts PunchDetector: positive spectral flux over log bins
 * lo..hi (default 60 Hz–6 kHz of the 64-bin spectrum) against the track's own running floor; no refractory, ~100 ms decay. */
class PunchDetector {
public:
  int lo, hi; float decay, gain; float punch = 0;
  explicit PunchDetector(int lo = 8, int hi = 60, float decay = 0.78f, float gain = 2.5f) : lo(lo), hi(hi), decay(decay), gain(gain) {}
  float update(const std::vector<float>& spec);
private:
  std::vector<float> prev_; float avg_ = 0.02f;
};

struct Palette { float hue = 0, sat = 0.8f, light = 0.6f, accentHue = 150; std::string name = "neutral"; };
struct TagLabels { std::vector<std::string> mood, genre; };
Palette paletteFor(const TagLabels* tags, int seed = 0);

struct SectionCue { std::string label; double t; };
struct VisualFrame {
  double t = 0;
  Bands bands;
  struct { double phase = 0; long index = 0; long bar = 0; float bpm = 120; float hit = 0; float punch = 0; } beat;
  struct Section { std::string label; int index; double progress; };
  std::optional<Section> section;
  Palette palette;
  std::vector<float> spectrum;
};
VisualFrame makeFrame(double t, const Bands& bands, const BeatClock& clock, const std::vector<SectionCue>& cues,
                      const Palette& palette, double durationS, std::vector<float> spectrum, float punch = 0);

/** GPU-Pulse style text meter: meter(0.62, 10) → "▰▰▰▰▰▰▱▱▱▱" (or "#"/"." with ascii = true). */
std::string meter(float v, int n = 12, bool ascii = false);

} // namespace ss
