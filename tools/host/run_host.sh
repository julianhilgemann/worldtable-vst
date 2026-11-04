#!/usr/bin/env bash
# Open JUCE's AudioPluginHost with Worldtable wired up:
#   on-screen keyboard -> MIDI Input -> Worldtable (AU) -> Audio Output
# Builds the host from the pinned JUCE source on first run (~2 min).
#
# Prereq (once):  cmake --preset dev -DWORLDTABLE_COPY_PLUGINS=ON && cmake --build --preset dev
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JUCE_SRC="$ROOT/build/dev/_deps/juce-src"
HOST_BUILD="$ROOT/build/tools/audiopluginhost"
APP="$HOST_BUILD/extras/AudioPluginHost/AudioPluginHost_artefacts/Release/AudioPluginHost.app"
GRAPH="$ROOT/tools/host/Worldtable-AU.filtergraph"

if [[ ! -d "$HOME/Library/Audio/Plug-Ins/Components/Worldtable.component" ]]; then
    echo "Worldtable AU not installed. Run:"
    echo "  cmake --preset dev -DWORLDTABLE_COPY_PLUGINS=ON && cmake --build --preset dev"
    exit 1
fi

if [[ ! -d "$APP" ]]; then
    if [[ ! -d "$JUCE_SRC" ]]; then
        echo "JUCE source not found. Configure the project first: cmake --preset dev"
        exit 1
    fi
    cmake -S "$JUCE_SRC" -B "$HOST_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DJUCE_BUILD_EXTRAS=ON -DJUCE_BUILD_EXAMPLES=OFF
    cmake --build "$HOST_BUILD" --target AudioPluginHost
fi

open -n "$APP" --args "$GRAPH"
