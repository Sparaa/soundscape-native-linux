#include "abc.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <set>
#include <sstream>

namespace ss {

static const std::map<char, int> LETTER = { {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11} };
static const char SHARP_ORDER[] = "FCGDAEB", FLAT_ORDER[] = "BEADGCF";
static std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(unsigned(c))); return s; }
static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

std::map<char, int> keySignature(const std::string& key) {
  std::map<char, int> sig = { {'C', 0}, {'D', 0}, {'E', 0}, {'F', 0}, {'G', 0}, {'A', 0}, {'B', 0} };
  static const std::regex re(R"(^\s*([A-Ga-g])([#b]?)\s*([A-Za-z]*))");
  std::smatch m;
  if (!std::regex_search(key, m, re)) return sig;
  int tonic = (LETTER.at(char(std::toupper(unsigned(m[1].str()[0])))) + (m[2] == "#" ? 1 : m[2] == "b" ? -1 : 0) + 12) % 12;
  std::string mode = lower(m[3]);
  int shift = starts(mode, "min") || mode == "m" || starts(mode, "aeo") ? 3 : starts(mode, "dor") ? 10 : starts(mode, "mix") ? 5 : starts(mode, "lyd") ? 7 : starts(mode, "phr") ? 8 : starts(mode, "loc") ? 1 : 0;
  int major = (tonic + shift) % 12;
  static const std::map<int, int> sharps = { {0, 0}, {7, 1}, {2, 2}, {9, 3}, {4, 4}, {11, 5}, {6, 6}, {1, 7} };
  static const std::map<int, int> flats = { {5, 1}, {10, 2}, {3, 3}, {8, 4}, {1, 5}, {6, 6}, {11, 7} };
  bool inS = sharps.count(major), inF = flats.count(major);
  bool preferFlats = m[2] == "b" || (!inS && inF) || (major == 6 && m[2] != "#");
  if (preferFlats && inF) for (int i = 0; i < flats.at(major); i++) sig[FLAT_ORDER[i]] = -1;
  else if (inS) for (int i = 0; i < sharps.at(major); i++) sig[SHARP_ORDER[i]] = 1;
  else if (inF) for (int i = 0; i < flats.at(major); i++) sig[FLAT_ORDER[i]] = -1;
  return sig;
}

static bool frac(const std::string& text, std::pair<int, int>& out) {
  static const std::regex re(R"(^\s*(\d+)\s*/\s*(\d+))");
  std::smatch m;
  if (std::regex_search(text, m, re) && std::stoi(m[2]) != 0) { out = { std::stoi(m[1]), std::stoi(m[2]) }; return true; }
  return false;
}

double abcLength(const std::string& suffix) {
  if (suffix.empty()) return 1;
  size_t i = 0; std::string a, sl, b;
  while (i < suffix.size() && std::isdigit(unsigned(suffix[i]))) a += suffix[i++];
  while (i < suffix.size() && suffix[i] == '/') sl += suffix[i++];
  while (i < suffix.size() && std::isdigit(unsigned(suffix[i]))) b += suffix[i++];
  if (i != suffix.size()) return 1;
  double num = a.empty() ? 1 : std::stod(a);
  if (sl.empty()) return num;
  double den = b.empty() ? std::pow(2.0, double(sl.size())) : std::stod(b);
  return num / (den != 0 ? den : 1);
}

namespace {
struct Tok { enum Kind { Note, Rest, Tie, Bar, Other } kind; std::string acc; char letter = 0; std::string octave, len; char restCh = 0; };

std::string lenSuffix(const std::string& s, size_t& i) {      // \d*\/*\d*
  std::string out;
  while (i < s.size() && std::isdigit(unsigned(s[i]))) out += s[i++];
  while (i < s.size() && s[i] == '/') out += s[i++];
  while (i < s.size() && std::isdigit(unsigned(s[i]))) out += s[i++];
  return out;
}
bool isNoteLetter(char c) { return (c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g'); }

/** Same alternation order as the web TOKEN regex; `i` advances past the token. */
Tok nextTok(const std::string& s, size_t& i) {
  char c = s[i];
  auto span = [&](char close) -> bool { size_t e = s.find(close, i + 1); if (e == std::string::npos) return false; i = e + 1; return true; };
  if (c == '"' && span('"')) return { Tok::Other };
  if (c == '[' && span(']')) return { Tok::Other };
  if (c == '!' && span('!')) return { Tok::Other };
  if (c == '{' && span('}')) return { Tok::Other };
  if (c == '+' && span('+')) return { Tok::Other };
  if (c == '|') { i++; if (i < s.size() && (s[i] == ':' || s[i] == ']')) i++; return { Tok::Bar }; }
  if (c == ':' && i + 1 < s.size() && s[i + 1] == '|') { i += 2; return { Tok::Bar }; }
  if (c == '(' && i + 1 < s.size() && std::isdigit(unsigned(s[i + 1]))) { i += 2; return { Tok::Other }; }
  if (c == '_' || c == '=' || c == '^' || isNoteLetter(c)) {
    size_t j = i; std::string acc;
    while (j < s.size() && (s[j] == '_' || s[j] == '=' || s[j] == '^')) acc += s[j++];
    if (j < s.size() && isNoteLetter(s[j])) {
      Tok t{ Tok::Note }; t.acc = acc; t.letter = s[j++];
      while (j < s.size() && (s[j] == ',' || s[j] == '\'')) t.octave += s[j++];
      t.len = lenSuffix(s, j); i = j; return t;
    }
  }
  if (c == 'z' || c == 'Z' || c == 'x' || c == 'X') { Tok t{ Tok::Rest }; t.restCh = c; i++; t.len = lenSuffix(s, i); return t; }
  if (c == '-') { i++; return { Tok::Tie }; }
  i++; return { Tok::Other };
}
} // namespace

AbcEvents abcNoteEvents(const std::string& abc, const std::string& voice) {
  std::pair<int, int> unit{ 1, 8 }, meterSig{ 4, 4 }; bool haveBeat = false; std::pair<int, int> beatNote{ 1, 4 };
  double bpm = 120; std::string key = "C";
  struct Line { std::string text, section; };
  std::vector<Line> music; std::string current; std::string section; bool sawVoice = false;
  std::istringstream in(abc); std::string raw;
  while (std::getline(in, raw)) {
    std::string line = trim(raw);
    if (line.empty()) continue;
    if (line[0] == '%') { if (!starts(line, "%%")) { size_t k = line.find_first_not_of("% \t"); section = k == std::string::npos ? "" : lower(line.substr(k)); } continue; }
    if (starts(line, "V:")) { sawVoice = true; current = lower(line).find("vocal") != std::string::npos ? "vocal" : "ins"; continue; }
    if (line.size() >= 2 && std::isalpha(unsigned(line[0])) && line[1] == ':') {
      char field = line[0]; std::string body = trim(line.substr(2));
      if (field == 'L') frac(body, unit);
      else if (field == 'M') { if (body == "C") meterSig = { 4, 4 }; else if (body == "C|") meterSig = { 2, 2 }; else frac(body, meterSig); }
      else if (field == 'Q') {
        static const std::regex q(R"((?:(\d+)\s*/\s*(\d+)\s*=\s*)?(\d+))");
        std::smatch m;
        if (std::regex_search(body, m, q)) { int b = std::stoi(m[3]); if (b) bpm = b; if (m[1].matched && m[2].matched) { beatNote = { std::stoi(m[1]), std::stoi(m[2]) }; haveBeat = true; } }
      } else if (field == 'K') key = body;
      continue;
    }
    if ((current.empty() ? std::string("vocal") : current) == voice) music.push_back({ line, section });
  }
  AbcEvents ev; ev.bpm = bpm;
  if (voice == "ins" && !sawVoice) return ev;
  auto sig = keySignature(key);
  std::pair<int, int> beat = haveBeat ? beatNote : std::pair<int, int>{ 1, meterSig.second };
  double secondsPerWhole = (60.0 / bpm) / (double(beat.first) / beat.second);
  double unitSec = secondsPerWhole * (double(unit.first) / unit.second);
  double barUnits = (double(meterSig.first) / meterSig.second) / (double(unit.first) / unit.second);
  std::set<std::string> sections; double t = 0; bool tieOpen = false; std::map<char, int> barAcc;
  for (const auto& L : music) {
    size_t i = 0;
    while (i < L.text.size()) {
      Tok tk = nextTok(L.text, i);
      if (tk.kind == Tok::Note) {
        char letter = char(std::toupper(unsigned(tk.letter)));
        int accVal;
        if (tk.acc.empty()) accVal = barAcc.count(letter) ? barAcc[letter] : sig[letter];
        else if (tk.acc == "=") accVal = 0;
        else { accVal = 0; for (char c : tk.acc) accVal += c == '^' ? 1 : -1; }
        if (!tk.acc.empty()) barAcc[letter] = accVal;
        int octave = (tk.letter == letter ? 4 : 5) + int(std::count(tk.octave.begin(), tk.octave.end(), '\'')) - int(std::count(tk.octave.begin(), tk.octave.end(), ','));
        double dur = abcLength(tk.len) * unitSec;
        int midi = 12 * (octave + 1) + LETTER.at(letter) + accVal;
        if (tieOpen && !ev.notes.empty() && ev.notes.back().midi == midi && std::abs(ev.notes.back().t + ev.notes.back().dur - t) < 1e-6) ev.notes.back().dur += dur;
        else ev.notes.push_back({ t, dur, midi, L.section });
        sections.insert(L.section); tieOpen = false; t += dur;
      } else if (tk.kind == Tok::Rest) {
        double bars = (tk.restCh == 'Z' || tk.restCh == 'X') ? abcLength(tk.len) * barUnits : abcLength(tk.len);
        t += bars * unitSec; tieOpen = false;
      } else if (tk.kind == Tok::Tie) tieOpen = true;
      else if (tk.kind == Tok::Bar) barAcc.clear();
    }
  }
  ev.seconds = t; ev.sections.assign(sections.begin(), sections.end());
  return ev;
}

std::vector<SectionCue> sectionCues(const std::string& abc, double actualSeconds) {
  AbcEvents voc = abcNoteEvents(abc, "vocal"), ins = abcNoteEvents(abc, "ins");
  double planned = std::max(voc.seconds, ins.seconds);
  double scale = actualSeconds > 0 && planned > 0 ? actualSeconds / planned : 1;
  std::vector<std::string> order;
  std::istringstream in(abc); std::string raw;
  while (std::getline(in, raw)) {
    std::string l = trim(raw);
    if (starts(l, "%") && !starts(l, "%%")) {
      size_t k = l.find_first_not_of("% \t"); std::string label = k == std::string::npos ? "" : lower(l.substr(k));
      if (starts(label, "chunk")) continue;
      order.push_back(label);
    }
  }
  std::vector<AbcNote> events = voc.notes; events.insert(events.end(), ins.notes.begin(), ins.notes.end());
  std::stable_sort(events.begin(), events.end(), [](const AbcNote& a, const AbcNote& b) { return a.t < b.t; });
  std::vector<SectionCue> cues; std::string current;
  for (const auto& n : events) if (n.section != current) {
    current = n.section;
    cues.push_back({ current.empty() ? "song" : current, std::round(n.t * scale * 100) / 100 });
  }
  if (!cues.empty() && cues[0].t > 0) cues[0].t = 0;
  if (cues.empty()) return order.empty() ? std::vector<SectionCue>{ { "song", 0 } } : std::vector<SectionCue>{ { order[0], 0 } };
  return cues;
}

} // namespace ss
