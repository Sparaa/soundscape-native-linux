#!/usr/bin/env bash
# Installs the `soundscape` command (symlink in ~/.local/bin) and a GNOME launcher entry + icon for the current user.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
mkdir -p ~/.local/bin ~/.local/share/applications ~/.local/share/icons/hicolor/256x256/apps
ln -sfn "$REPO/scripts/soundscape" ~/.local/bin/soundscape
cp "$REPO/packaging/soundscape.png" ~/.local/share/icons/hicolor/256x256/apps/soundscape.png
cp "$REPO/packaging/soundscape.desktop" ~/.local/share/applications/soundscape.desktop
command -v update-desktop-database >/dev/null && update-desktop-database ~/.local/share/applications 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q ~/.local/share/icons/hicolor 2>/dev/null || true
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "note: ~/.local/bin is not on your PATH; add it to use \`soundscape\` from a shell" ;; esac
echo "installed: $(command -v soundscape || echo ~/.local/bin/soundscape) → $REPO/build/soundscape-native"
