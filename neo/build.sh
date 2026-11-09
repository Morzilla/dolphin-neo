#!/bin/bash
# Build this tree and install it into ~/.local/dolphin-neo (no root; a packaged Dolphin is left alone).
#   neo/build.sh
# Build dependencies (once): the ones of Dolphin itself, e.g. on Fedora `sudo dnf builddep dolphin`.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${DOLPHIN_NEO_PREFIX:-$HOME/.local/dolphin-neo}"

cmake -S "$ROOT" -B "$ROOT/build-neo" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DBUILD_TESTING=OFF -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF
cmake --build "$ROOT/build-neo" -j"${JOBS:-$(nproc)}"
cmake --install "$ROOT/build-neo" > "$ROOT/build-neo/install.log"
echo "installed in $PREFIX; start it with $ROOT/neo/dolphin-neo"
