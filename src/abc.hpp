#pragma once
// ABC score parser for the YuE2/SheetSage2 dialect — a port of soundscape web/lib/abc.ts. The visualizer wants the
// section start times; the timed note events are what they are derived from.
#include <map>
#include <string>
#include <vector>
#include "analysis.hpp"

namespace ss {

struct AbcNote { double t; double dur; int midi; std::string section; };
struct AbcEvents { std::vector<AbcNote> notes; double seconds = 0; double bpm = 120; std::vector<std::string> sections; };

std::map<char, int> keySignature(const std::string& key);
double abcLength(const std::string& suffix);
AbcEvents abcNoteEvents(const std::string& abc, const std::string& voice = "vocal");
/** Section start times (s) from the planned score; `actualSeconds` (> 0) rescales to the rendered length. */
std::vector<SectionCue> sectionCues(const std::string& abc, double actualSeconds = 0);

} // namespace ss
