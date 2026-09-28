// Song downloads — the pure parts (no curl, no UI) so the unit tests can cover them. The api serves
// GET /songs/{id}/download?fmt=flac|mp3 as an attachment named "<station> - <title>.<ext>"; the client keeps that
// name and saves into the user's XDG music folder under Soundscape/.
#pragma once
#include <filesystem>
#include <string>

namespace ss {

// The file name from a Content-Disposition header value: RFC 5987 `filename*=utf-8''<percent-encoded>` wins over a
// plain `filename="…"`. Path separators are stripped so a hostile header can't escape the folder. Empty if none.
std::string dispositionFilename(const std::string& header);

// XDG_MUSIC_DIR from the contents of ~/.config/user-dirs.dirs ("$HOME/Music" style values), else <home>/Music.
std::filesystem::path musicDir(const std::string& home, const std::string& userDirsText);

// <dir>/<name>, or "<stem> (2)<ext>", "(3)"… if that already exists — a download never overwrites a file.
std::filesystem::path uniquePath(const std::filesystem::path& dir, const std::string& name);

} // namespace ss
