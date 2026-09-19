// Unit tests for the pure modules (no GPU, no audio device, no network). Run: ctest or ./soundscape-tests
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>
#include "abc.hpp"
#include "analysis.hpp"

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= (eps))

using namespace ss;

static void test_abc() {
  NEAR(abcLength(""), 1, 0); NEAR(abcLength("2"), 2, 0); NEAR(abcLength("3/2"), 1.5, 0); NEAR(abcLength("/"), 0.5, 0); NEAR(abcLength("//"), 0.25, 0); NEAR(abcLength("/4"), 0.25, 0);
  auto sig = keySignature("F#m"); CHECK(sig['F'] == 1 && sig['C'] == 1 && sig['G'] == 1 && sig['D'] == 0);
  auto bb = keySignature("Bb"); CHECK(bb['B'] == -1 && bb['E'] == -1 && bb['A'] == 0);
  CHECK(keySignature("C")['F'] == 0);
  const std::string abc =
    "X:1\nT:t\nM:4/4\nL:1/8\nQ:1/4=120\nK:C\n"
    "V:1 name=vocal\n% intro\nz8 | z8 |\n% verse\nC2 D2 E2 F2 | G4 z4 |\n% chorus\nc8 |\n"
    "V:2 name=ins\n% intro\nC8 | C8 |\n";
  AbcEvents voc = abcNoteEvents(abc, "vocal");
  NEAR(voc.bpm, 120, 0);
  // L=1/8 at 120 bpm (quarter) → an eighth is 0.25 s; two bars of rests = 4 s before the first note
  CHECK(voc.notes.size() == 6);
  NEAR(voc.notes[0].t, 4.0, 1e-9); CHECK(voc.notes[0].midi == 60); CHECK(voc.notes[0].section == "verse");
  NEAR(voc.notes[5].t, 8.0, 1e-9); CHECK(voc.notes[5].midi == 72); CHECK(voc.notes[5].section == "chorus");
  NEAR(voc.seconds, 10.0, 1e-9);
  auto cues = sectionCues(abc);
  CHECK(cues.size() == 3);
  if (cues.size() == 3) { CHECK(cues[0].label == "intro"); NEAR(cues[0].t, 0, 0); CHECK(cues[1].label == "verse"); NEAR(cues[1].t, 4, 1e-9); CHECK(cues[2].label == "chorus"); NEAR(cues[2].t, 8, 1e-9); }
  auto scaled = sectionCues(abc, 20);                      // rendered twice as long → cues stretch
  if (scaled.size() == 3) { NEAR(scaled[1].t, 8, 1e-9); NEAR(scaled[2].t, 16, 1e-9); }
  CHECK(sectionCues("X:1\nK:C\n").size() == 1);
  // ties + accidentals carry through the bar, reset at the bar line
  AbcEvents t2 = abcNoteEvents("L:1/4\nQ:60\nK:C\n^C C | C |\n", "vocal");
  CHECK(t2.notes.size() == 3 && t2.notes[0].midi == 61 && t2.notes[1].midi == 61 && t2.notes[2].midi == 60);
  AbcEvents t3 = abcNoteEvents("L:1/4\nQ:60\nK:C\nC2-C2 |\n", "vocal");
  CHECK(t3.notes.size() == 1); NEAR(t3.notes[0].dur, 4, 1e-9);
}

static void test_bands() {
  std::vector<uint8_t> fft(1024, 0);
  const float sr = 48000, hzPerBin = sr / 2048;
  for (int i = 0; i < 1024; i++) { float hz = i * hzPerBin; if (hz >= 20 && hz <= 150) fft[i] = 255; }
  Bands b = bandEnergies(fft, sr, 2048);
  CHECK(b.bass > 0.7f); CHECK(b.mid < 0.05f);   // 6 of the 8 bins the 20-150 Hz window covers (ceil on the high edge) CHECK(b.treble == 0.f); CHECK(b.rms > 0);
  auto spec = logSpectrum(fft, sr, 2048, 64);
  CHECK(spec.size() == 64); CHECK(spec[0] > 0.9f); CHECK(spec[63] == 0.f);
  CHECK(meter(0.62f, 10, true) == "######....");
  CHECK(meter(0.f, 4, true) == "....");
}

static void test_analyser() {
  Analyser an(2048, 0.0f);
  std::vector<float> s(2048); const float sr = 48000, f0 = 1000;
  for (int i = 0; i < 2048; i++) s[i] = 0.5f * std::sin(2 * std::numbers::pi_v<float> * f0 * i / sr);
  std::vector<uint8_t> out; an.analyse(s.data(), out);
  CHECK(out.size() == 1024);
  int peak = 0; for (int i = 1; i < 1024; i++) if (out[i] > out[peak]) peak = i;
  int expect = int(std::lround(f0 / (sr / 2048)));
  CHECK(std::abs(peak - expect) <= 1);
  CHECK(out[peak] > 150);              // -6 dBFS sine lands well inside the -100..-30 dB window
  CHECK(out[500] < 40);                // far bins are near the floor
  // smoothing: with τ = 0.8 a sudden silence decays, it does not vanish
  Analyser sm(2048, 0.8f); std::vector<uint8_t> a, b; sm.analyse(s.data(), a);
  std::vector<float> zero(2048, 0.f); sm.analyse(zero.data(), b);
  CHECK(b[expect] > 0 && b[expect] < a[expect]);
}

static void test_clock() {
  BeatClock c(120, 0);
  NEAR(c.period(), 0.5, 1e-12); NEAR(c.phase(0.25), 0.5, 1e-9); CHECK(c.beatIndex(1.0) == 2); CHECK(c.bar(2.0) == 1);
  // a bass onset slightly after a beat pulls t0 forward by lock * error
  c.update(0.0, 0.0f);
  bool hit = c.update(0.55, 0.5f);
  CHECK(hit); NEAR(c.hit, 1.0, 1e-6);
  NEAR(c.phase(0.55), 0.1 - 0.1 * 0.15, 1e-6);
  CHECK(!c.update(0.60, 0.9f));        // refractory: < 0.45 periods after the last hit
  Palette p = paletteFor(nullptr, 3); NEAR(p.hue, 141, 0); CHECK(p.name == "neutral");
  TagLabels t; t.mood = { "Dark" }; Palette d = paletteFor(&t); NEAR(d.hue, 260, 0); NEAR(d.sat, 0.55, 1e-6); CHECK(d.name == "dark");
  std::vector<SectionCue> cues = { { "intro", 0 }, { "verse", 10 } };
  VisualFrame f = makeFrame(12, Bands{}, c, cues, p, 30, {});
  CHECK(f.section && f.section->label == "verse" && f.section->index == 1); NEAR(f.section->progress, 0.1, 1e-9);
}

static void test_punch() {
  PunchDetector d; std::vector<float> quiet(64, 0.1f);
  for (int i = 0; i < 30; i++) d.update(quiet);
  std::vector<float> kick = quiet; for (int i = 0; i < 16; i++) kick[i] += 0.35f;   // 30–125 Hz burst: a kick / 808
  float hit = d.update(kick); CHECK(hit > 0.8f);
  float a = d.update(kick); a = d.update(kick); a = d.update(kick);
  CHECK(a < hit * 0.6f && a > 0);
  PunchDetector s; for (int i = 0; i < 30; i++) s.update(quiet);
  std::vector<float> snare = quiet; for (int i = 20; i < 50; i++) snare[i] += 0.35f;   // ~180 Hz–3 kHz: snare, vocals, hats
  CHECK(s.update(snare) < 0.05f);                                                     // above the cutoff: not punch
  PunchDetector r; float last = 0;
  for (int i = 0; i < 60; i++) { std::vector<float> v(64); for (int k = 0; k < 64; k++) v[k] = 0.1f + 0.02f * std::sin(i * 0.7f + k); last = r.update(v); }
  CHECK(last < 0.35f);
  VisualFrame f = makeFrame(1, Bands{}, BeatClock(120), {}, Palette{}, 10, {}, 0.7f); NEAR(f.beat.punch, 0.7, 1e-6);
}

int main() {
  test_punch();
  test_abc(); test_bands(); test_analyser(); test_clock();
  std::printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
