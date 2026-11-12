#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build this tree and install it into ~/.local/dolphin-ng (no root; a packaged Dolphin is left alone).
#   ng/build.sh
# Build dependencies (once): the ones of Dolphin itself, e.g. on Fedora `sudo dnf builddep dolphin`.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${DOLPHIN_NG_PREFIX:-$HOME/.local/dolphin-ng}"

cmake -S "$ROOT" -B "$ROOT/build-ng" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DBUILD_TESTING=OFF -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF
cmake --build "$ROOT/build-ng" -j"${JOBS:-$(nproc)}"
cmake --install "$ROOT/build-ng" > "$ROOT/build-ng/install.log"
echo "installed in $PREFIX; start it with $ROOT/ng/dolphin-ng"
