#include "analysis.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ss {

Analyser::Analyser(int fftSize, float smoothing, float minDb, float maxDb)
    : n_(fftSize), smoothing_(smoothing), minDb_(minDb), maxDb_(maxDb), window_(fftSize), re_(fftSize), im_(fftSize), mag_(fftSize / 2, 0.f) {
  // Blackman window, as Chromium's RealtimeAnalyser applies it
  const double a0 = 0.42, a1 = 0.5, a2 = 0.08;
  for (int i = 0; i < n_; i++) {
    double x = double(i) / n_;
    window_[i] = float(a0 - a1 * std::cos(2 * std::numbers::pi * x) + a2 * std::cos(4 * std::numbers::pi * x));
  }
}

static void fft(std::vector<float>& re, std::vector<float>& im) {
  const int n = int(re.size());
  for (int i = 1, j = 0; i < n; i++) {                       // bit reversal
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double ang = -2 * std::numbers::pi / len;
    float wr = float(std::cos(ang)), wi = float(std::sin(ang));
    for (int i = 0; i < n; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = i + k + len / 2;
        float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
        float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
}

void Analyser::analyse(const float* samples, std::vector<uint8_t>& out) {
  for (int i = 0; i < n_; i++) { re_[i] = samples[i] * window_[i]; im_[i] = 0; }
  fft(re_, im_);
  const float scale = 1.f / n_, range = 1.f / (maxDb_ - minDb_);
  out.resize(n_ / 2);
  for (int k = 0; k < n_ / 2; k++) {
    float m = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]) * scale;
    mag_[k] = smoothing_ * mag_[k] + (1 - smoothing_) * m;
    float db = mag_[k] > 0 ? 20.f * std::log10(mag_[k]) : -1000.f;
    float v = 255.f * (db - minDb_) * range;
    out[k] = uint8_t(std::clamp(v, 0.f, 255.f));
  }
}

Bands bandEnergies(const std::vector<uint8_t>& fft, float sampleRate, int fftSize) {
  const int bins = int(fft.size());
  const float hzPerBin = sampleRate / fftSize;
  auto sum = [&](float lo, float hi) {
    int a = std::max(0, int(std::floor(lo / hzPerBin))), b = std::min(bins - 1, int(std::ceil(hi / hzPerBin)));
    if (b < a) return 0.f;
    float s = 0; for (int i = a; i <= b; i++) s += fft[i];
    return s / ((b - a + 1) * 255.f);
  };
  double sq = 0; for (int i = 0; i < bins; i++) { double x = fft[i] / 255.0; sq += x * x; }
  return { sum(20, 150), sum(150, 2000), sum(2000, 12000), float(std::sqrt(sq / std::max(1, bins))) };
}

std::vector<float> logSpectrum(const std::vector<uint8_t>& fft, float sampleRate, int fftSize, int n, float lo, float hi) {
  const float hzPerBin = sampleRate / fftSize;
  std::vector<float> out(n);
  const double ratio = std::log(hi / lo) / n;
  for (int i = 0; i < n; i++) {
    double f0 = lo * std::exp(i * ratio), f1 = lo * std::exp((i + 1) * ratio);
    int a = std::max(0, int(std::floor(f0 / hzPerBin)));
    int b = std::max(a, std::min(int(fft.size()) - 1, int(std::floor(f1 / hzPerBin))));
    float s = 0; for (int k = a; k <= b; k++) s += fft[k];
    out[i] = s / ((b - a + 1) * 255.f);
  }
  return out;
}

BeatClock::BeatClock(float bpm_, double startT, float lock_, float threshold_)
    : bpm(std::max(40.f, bpm_ > 0 ? bpm_ : 120.f)), lock(lock_), threshold(threshold_), t0_(startT) {}
double BeatClock::phase(double t) const { double p = std::fmod((t - t0_) / period(), 1.0); return p < 0 ? p + 1 : p; }
long BeatClock::beatIndex(double t) const { return long(std::floor((t - t0_) / period())); }
bool BeatClock::update(double t, float bass) {
  float flux = bass - lastBass_;
  lastBass_ = bass;
  hit *= 0.85f;
  if (flux > threshold && t - lastHitT_ > period() * 0.45) {
    lastHitT_ = t;
    hit = 1;
    double err = phase(t);
    if (err > 0.5) err -= 1;
    t0_ += err * period() * lock;
    return true;
  }
  return false;
}

static float moodHue(const std::string& m, bool& ok) {
  static const std::pair<const char*, float> T[] = { {"dark", 260}, {"epic", 30}, {"energetic", 10}, {"playful", 320}, {"happy", 50}, {"sad", 210}, {"calm", 170}, {"dreamy", 280}, {"aggressive", 0}, {"romantic", 340}, {"melancholic", 220}, {"hopeful", 100} };
  for (auto& [k, v] : T) if (m == k) { ok = true; return v; }
  ok = false; return 0;
}
static float genreHue(const std::string& g, bool& ok) {
  static const std::pair<const char*, float> T[] = { {"edm", 190}, {"synth-pop", 300}, {"k-pop", 320}, {"hip hop", 40}, {"rock", 15}, {"metal", 0}, {"jazz", 45}, {"classical", 200}, {"orchestral film score", 30}, {"folk", 90}, {"country", 60}, {"ambient", 180}, {"techno", 200}, {"house", 280}, {"lo-fi", 25}, {"pop", 330} };
  for (auto& [k, v] : T) if (g == k) { ok = true; return v; }
  ok = false; return 0;
}
static std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(unsigned(c))); return s; }

Palette paletteFor(const TagLabels* tags, int seed) {
  std::string mood = tags && !tags->mood.empty() ? lower(tags->mood[0]) : "";
  std::string genre = tags && !tags->genre.empty() ? lower(tags->genre[0]) : "";
  bool okM = false, okG = false;
  float hm = mood.empty() ? 0 : moodHue(mood, okM), hg = genre.empty() ? 0 : genreHue(genre, okG);
  float hue = okM ? hm : okG ? hg : float((seed * 47) % 360);
  hue = std::fmod(hue + 360.f, 360.f);
  bool dark = mood == "dark" || mood == "melancholic" || mood == "sad";
  Palette p; p.hue = hue; p.sat = dark ? 0.55f : 0.8f; p.light = dark ? 0.45f : 0.6f; p.accentHue = std::fmod(hue + 150.f, 360.f);
  p.name = !mood.empty() ? mood : !genre.empty() ? genre : "neutral";
  return p;
}

VisualFrame makeFrame(double t, const Bands& bands, const BeatClock& clock, const std::vector<SectionCue>& cues,
                      const Palette& palette, double durationS, std::vector<float> spectrum) {
  VisualFrame f; f.t = t; f.bands = bands; f.palette = palette; f.spectrum = std::move(spectrum);
  f.beat.phase = clock.phase(t); f.beat.index = clock.beatIndex(t); f.beat.bar = clock.bar(t); f.beat.bpm = clock.bpm; f.beat.hit = clock.hit;
  int idx = -1;
  for (size_t i = 0; i < cues.size(); i++) { if (cues[i].t <= t) idx = int(i); else break; }
  if (idx >= 0) {
    double start = cues[idx].t, end = size_t(idx + 1) < cues.size() ? cues[idx + 1].t : std::max(durationS, start + 1);
    f.section = VisualFrame::Section{ cues[idx].label, idx, std::clamp((t - start) / std::max(0.001, end - start), 0.0, 1.0) };
  }
  return f;
}

std::string meter(float v, int n, bool ascii) {
  if (!std::isfinite(v)) v = 0;
  int k = std::clamp(int(std::lround(v * n)), 0, n);
  std::string s;
  for (int i = 0; i < n; i++) s += ascii ? (i < k ? "#" : ".") : (i < k ? "▰" : "▱");
  return s;
}

} // namespace ss
