// Offline punch-detector calibration: run the exact analysis chain (AnalyserNode emulation → logSpectrum →
// PunchDetector) over raw mono f32 PCM at 60 fps and report, per configuration, how often the center would pulse
// and how loud the band was when it did. Used 2026-09-19 to pick the audibility gate.
//
//   ffmpeg -i song.mp3 -ac 1 -ar 48000 -f f32le song.f32
//   punch-calibrate song.f32 [more.f32 ...]
#include "../src/analysis.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ss;

struct Cfg { const char* name; int lo, hi; float levelLo, levelHi; };

static float pct(std::vector<float> v, double q) { if (v.empty()) return 0.f; std::sort(v.begin(), v.end()); return v[size_t(q * (v.size() - 1))]; }

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: punch-calibrate song.f32 [...]\n"); return 2; }
  const float sr = 48000.f; const int fft = 2048; const double dt = 1.0 / 60;
  const Cfg cfgs[] = {
    { "old 30-150 no gate",   0, 18, 0.f, 0.f },
    { "50-150 no gate",       6, 18, 0.f, 0.f },
    { "50-150 gate .35/.60",  6, 18, .35f, .60f },
    { "50-150 gate .45/.70",  6, 18, .45f, .70f },
    { "50-150 gate .50/.75",  6, 18, .50f, .75f },
    { "50-150 gate .55/.80",  6, 18, .55f, .80f },
    { "50-150 gate .60/.85",  6, 18, .60f, .85f },
  };
  for (int a = 1; a < argc; a++) {
    FILE* f = std::fopen(argv[a], "rb"); if (!f) { std::perror(argv[a]); continue; }
    std::vector<float> pcm; float buf[4096]; size_t n;
    while ((n = std::fread(buf, sizeof(float), 4096, f)) > 0) pcm.insert(pcm.end(), buf, buf + n);
    std::fclose(f);
    std::printf("%s: %.1f s\n", argv[a], pcm.size() / sr);
    for (const Cfg& c : cfgs) {
      Analyser an(fft); PunchDetector det(c.lo, c.hi); det.levelLo = c.levelLo; det.levelHi = c.levelHi;
      std::vector<uint8_t> bytes; std::vector<float> lvPeak, lvAll; long frames = 0, high = 0, peaks = 0; float last = 0;
      std::vector<float> win(fft, 0.f);
      for (double t = 0; t * sr + fft < pcm.size(); t += dt) {
        size_t end = size_t(t * sr) + fft;
        std::memcpy(win.data(), pcm.data() + (end - fft), fft * sizeof(float));
        an.analyse(win.data(), bytes);
        float p = det.update(logSpectrum(bytes, sr, fft));
        frames++; if (p > 0.5f) high++; if (p > 0.6f && last <= 0.6f) { peaks++; lvPeak.push_back(det.level); } last = p; lvAll.push_back(det.level);
      }
      std::printf("  %-22s peaks %5.2f/s  >0.5 %4.1f%%  level@peaks p10/p50/p90 %.2f/%.2f/%.2f  (all frames p50 %.2f)\n",
                  c.name, peaks / (frames * dt), 100.0 * high / frames, pct(lvPeak, .1), pct(lvPeak, .5), pct(lvPeak, .9), pct(lvAll, .5));
    }
  }
  return 0;
}
