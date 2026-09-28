#include "download.hpp"
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ss {
namespace fs = std::filesystem;

namespace {
std::string pctDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else out += s[i];
  }
  return out;
}

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string safeName(std::string n) {
  for (auto& c : n) if (c == '/' || c == '\\' || c == '\0') c = '-';
  n = trim(n);
  while (!n.empty() && n.front() == '.') n.erase(n.begin());  // no hidden files, no ".." games
  return n;
}
} // namespace

std::string dispositionFilename(const std::string& header) {
  std::string plain, extended;
  std::stringstream ss(header);
  for (std::string part; std::getline(ss, part, ';');) {
    part = trim(part);
    auto eq = part.find('=');
    if (eq == std::string::npos) continue;
    std::string key = lower(trim(part.substr(0, eq))), val = trim(part.substr(eq + 1));
    if (key == "filename*") {
      auto q = val.find("''");  // charset'lang'value
      extended = pctDecode(q == std::string::npos ? val : val.substr(q + 2));
    } else if (key == "filename") {
      if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
      plain = val;
    }
  }
  return safeName(!extended.empty() ? extended : plain);
}

fs::path musicDir(const std::string& home, const std::string& userDirsText) {
  std::stringstream ss(userDirsText);
  for (std::string line; std::getline(ss, line);) {
    line = trim(line);
    if (line.rfind("XDG_MUSIC_DIR=", 0) != 0) continue;
    std::string v = line.substr(14);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    if (v.rfind("$HOME", 0) == 0) v = home + v.substr(5);
    while (v.size() > 1 && v.back() == '/') v.pop_back();
    std::string h = home; while (h.size() > 1 && h.back() == '/') h.pop_back();
    if (!v.empty() && v.front() == '/' && v != h) return v;  // "$HOME" / "$HOME/" = the folder is disabled
  }
  return fs::path(home) / "Music";
}

fs::path uniquePath(const fs::path& dir, const std::string& name) {
  fs::path p = dir / name;
  std::error_code ec;
  if (!fs::exists(p, ec)) return p;
  std::string stem = fs::path(name).stem().string(), ext = fs::path(name).extension().string();
  for (int i = 2; i < 1000; i++) {
    fs::path q = dir / (stem + " (" + std::to_string(i) + ")" + ext);
    if (!fs::exists(q, ec)) return q;
  }
  return dir / (stem + " (" + std::to_string(std::rand()) + ")" + ext);
}

} // namespace ss
