#!/usr/bin/env bash
# Tape-Top — macOS plugin build
#
# Why this script exists: `cmake -G Xcode` fails with "Xcode 1.5 not supported"
# on machines that only have the Command Line Tools installed (or have
# xcode-select pointing at them). Ninja and Unix Makefiles build VST3, AU and the
# standalone app perfectly well, so this script picks a generator that works
# instead of making you debug CMake's Xcode detection.
#
#   ./scripts/build-macos.sh              # Release build, best available generator
#   ./scripts/build-macos.sh --xcode      # force Xcode (only if full Xcode is installed)
#   ./scripts/build-macos.sh --debug      # Debug build
#   ./scripts/build-macos.sh --clean      # wipe plugin/build first

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGIN_DIR="$ROOT/plugin"
BUILD_DIR="$PLUGIN_DIR/build"

CONFIG="Release"
GENERATOR=""
CLEAN=0

for arg in "$@"; do
  case "$arg" in
    --xcode)  GENERATOR="Xcode" ;;
    --debug)  CONFIG="Debug" ;;
    --clean)  CLEAN=1 ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [[ ! -f "$PLUGIN_DIR/CMakeLists.txt" ]]; then
  echo "error: $PLUGIN_DIR/CMakeLists.txt not found." >&2
  echo "       You are probably not inside a clone of the repo. From your home folder:" >&2
  echo "       git clone -b arena/e95242b2-tape-top https://github.com/Jackherz/Tape-Top.git" >&2
  echo "       cd Tape-Top && ./scripts/build-macos.sh" >&2
  exit 1
fi

command -v cmake >/dev/null || { echo "error: cmake not found. brew install cmake" >&2; exit 1; }

# ---- pick a generator ------------------------------------------------------
if [[ -z "$GENERATOR" ]]; then
  if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
  else
    GENERATOR="Unix Makefiles"
  fi
fi

if [[ "$GENERATOR" == "Xcode" ]]; then
  if ! xcode-select -p 2>/dev/null | grep -q "Xcode.app"; then
    echo "warning: xcode-select points at $(xcode-select -p 2>/dev/null || echo 'nothing')." >&2
    echo "         If you do have full Xcode: sudo xcode-select -s /Applications/Xcode.app/Contents/Developer" >&2
    echo "         Otherwise drop --xcode and let the script use Ninja/Makefiles." >&2
  fi
fi

echo "repo:      $ROOT"
echo "generator: $GENERATOR"
echo "config:    $CONFIG"
echo "cmake:     $(cmake --version | head -1)"
echo "compiler:  $(clang --version 2>/dev/null | head -1 || echo 'clang not found')"
echo

[[ $CLEAN -eq 1 ]] && rm -rf "$BUILD_DIR"

# JUCE is fetched on first configure: a few minutes, needs network.
cmake -S "$PLUGIN_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo
echo "built. artefacts:"
find "$BUILD_DIR" -maxdepth 4 \( -name "*.vst3" -o -name "*.component" -o -name "*.app" \) -print 2>/dev/null | sed 's/^/  /' || true
echo
echo "COPY_PLUGIN_AFTER_BUILD is on, so the AU and VST3 should already be in"
echo "  ~/Library/Audio/Plug-Ins/Components and ~/Library/Audio/Plug-Ins/VST3"
echo "If your DAW does not see them, rescan plugins (Logic: Plugin Manager; Ableton: rescan)."
