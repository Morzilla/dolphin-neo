#!/bin/bash
# dolphin-ng: download, build and install everything.
#
#   ng/install.sh                 from a clone of the repository
#   curl -fsSL https://raw.githubusercontent.com/Morzilla/dolphin-ng/ng/ng/install.sh | bash
#                                  on its own: clones the repository into ~/.local/src/dolphin-ng first
#
# What it does, in order:
#   1. installs the build dependencies with your package manager (asks for sudo)
#   2. builds the fork and installs it into ~/.local/dolphin-ng (your distribution's Dolphin is left alone)
#   3. adds a "Dolphin NG" entry to the application menu and a dolphin-ng command in ~/.local/bin
#   4. installs the Motto-Neo theme files (Kvantum theme, window decoration, colour scheme)
#
# Options:
#   --no-deps       skip step 1
#   --no-theme      skip step 4
#   --apply-theme   also switch your desktop to the Motto-Neo theme (it changes how every application looks)
#   --prefix DIR    install into DIR instead of ~/.local/dolphin-ng
#   --uninstall     remove what steps 2 and 3 installed
set -euo pipefail

REPO_URL="https://github.com/Morzilla/dolphin-ng.git"
PREFIX="${DOLPHIN_NG_PREFIX:-$HOME/.local/dolphin-ng}"
DEPS=1 THEME=1 APPLY=0 UNINSTALL=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-deps) DEPS=0 ;;
        --no-theme) THEME=0 ;;
        --apply-theme) APPLY=1 ;;
        --prefix) PREFIX="$2"; shift ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
say() { printf '\n==> %s\n' "$*"; }

DESKTOP="$HOME/.local/share/applications/dolphin-ng.desktop"
COMMAND="$HOME/.local/bin/dolphin-ng"

if [ "$UNINSTALL" = 1 ]; then
    say "removing $PREFIX, the menu entry and the dolphin-ng command"
    rm -rf "$PREFIX" "$DESKTOP" "$COMMAND"
    echo "Done. Settings (dolphinrc, group DolphinNG) and the theme files were left in place."
    exit 0
fi

# ---- where is the source?
if [ -f "$(dirname "${BASH_SOURCE[0]:-/nonexistent}")/../NG.md" ]; then
    ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
else
    ROOT="$HOME/.local/src/dolphin-ng"
    command -v git >/dev/null || { echo "git is needed to download the source; install it and run this again." >&2; exit 1; }
    if [ -d "$ROOT/.git" ]; then
        say "updating the source in $ROOT"
        git -C "$ROOT" pull --ff-only
    else
        say "downloading the source into $ROOT"
        mkdir -p "$(dirname "$ROOT")"
        git clone --depth 1 --branch neo "$REPO_URL" "$ROOT"
    fi
fi

# ---- 1. build dependencies
if [ "$DEPS" = 1 ]; then
    say "installing the build dependencies (sudo)"
    if command -v dnf >/dev/null; then
        # A partly updated system can end up with mixed KDE Frameworks versions: update first if in doubt.
        sudo dnf install -y git cmake gcc-c++ extra-cmake-modules 'dnf-command(builddep)' poppler-utils
        sudo dnf builddep -y dolphin
        [ "$THEME" = 1 ] && sudo dnf install -y kvantum || true
    elif command -v apt-get >/dev/null; then
        sudo apt-get update
        sudo apt-get install -y git cmake g++ extra-cmake-modules poppler-utils
        sudo apt-get build-dep -y dolphin || {
            echo "apt could not install Dolphin's build dependencies: enable the source repositories (deb-src) and run this again." >&2
            exit 1
        }
        [ "$THEME" = 1 ] && sudo apt-get install -y qt6-style-kvantum || true
    elif command -v pacman >/dev/null; then
        sudo pacman -S --needed --noconfirm git cmake extra-cmake-modules base-devel poppler \
            kio kparts knewstuff kcmutils kdbusaddons kiconthemes ktextwidgets kcrash kbookmarks kcompletion \
            kwindowsystem kdoctools baloo baloo-widgets kfilemetadata kuserfeedback phonon-qt6 plasma-activities
        [ "$THEME" = 1 ] && sudo pacman -S --needed --noconfirm kvantum || true
    elif command -v zypper >/dev/null; then
        sudo zypper --non-interactive install git cmake gcc-c++ extra-cmake-modules poppler-tools
        sudo zypper --non-interactive source-install --build-deps-only dolphin
        [ "$THEME" = 1 ] && sudo zypper --non-interactive install kvantum-qt6 || true
    else
        echo "I do not know this distribution's package manager. Install Dolphin's build dependencies yourself, then run this again with --no-deps." >&2
        exit 1
    fi
fi

# ---- 2. build and install
say "building (this takes a few minutes)"
DOLPHIN_NG_PREFIX="$PREFIX" "$ROOT/ng/build.sh"

# ---- 3. menu entry and command
say "adding the menu entry and the dolphin-ng command"
mkdir -p "$(dirname "$DESKTOP")" "$(dirname "$COMMAND")"
cat > "$COMMAND" <<LAUNCH
#!/bin/sh
DOLPHIN_NG_PREFIX="$PREFIX" exec "$ROOT/ng/dolphin-ng" "\$@"
LAUNCH
chmod +x "$COMMAND"
cat > "$DESKTOP" <<ENTRY
[Desktop Entry]
Type=Application
Name=Dolphin NG
GenericName=File Manager
Comment=Experimental fork of Dolphin with a different file list
Exec=$COMMAND %u
Icon=system-file-manager
Terminal=false
Categories=Qt;KDE;System;FileTools;FileManager;
MimeType=inode/directory;
StartupWMClass=dolphin
ENTRY
if [ ! -e "$HOME/.local/share/kxmlgui5/dolphin/dolphinui.rc" ]; then
    mkdir -p "$HOME/.local/share/kxmlgui5/dolphin"
    cp "$ROOT/ng/dolphinui.rc.example" "$HOME/.local/share/kxmlgui5/dolphin/dolphinui.rc" # toolbar with New Folder, Up and Root
fi

# ---- 4. theme
if [ "$THEME" = 1 ]; then
    say "installing the Motto-Neo theme files"
    mkdir -p "$HOME/.config/Kvantum" "$HOME/.local/share/aurorae/themes" "$HOME/.local/share/color-schemes"
    cp -r "$ROOT/ng/theme/kvantum/Motto-Neo" "$HOME/.config/Kvantum/"
    cp -r "$ROOT/ng/theme/aurorae/Motto-Neo-Aurorae" "$HOME/.local/share/aurorae/themes/"
    cp "$ROOT/ng/theme/color-schemes/MottoNeo.colors" "$HOME/.local/share/color-schemes/"
    if [ "$APPLY" = 1 ]; then
        say "switching the desktop to Motto-Neo"
        command -v kvantummanager >/dev/null && kvantummanager --set Motto-Neo || echo "kvantummanager not found: choose Motto-Neo in Kvantum Manager yourself."
        command -v kwriteconfig6 >/dev/null && kwriteconfig6 --file kdeglobals --group KDE --key widgetStyle kvantum-dark || true
        command -v plasma-apply-colorscheme >/dev/null && plasma-apply-colorscheme MottoNeo || true
        if command -v kwriteconfig6 >/dev/null; then
            kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library org.kde.kwin.aurorae.v2
            kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme __aurorae__svg__Motto-Neo-Aurorae
            (qdbus6 org.kde.KWin /KWin reconfigure || qdbus org.kde.KWin /KWin reconfigure) >/dev/null 2>&1 || true
        fi
    fi
fi

say "done"
echo "Start it from the application menu (Dolphin NG) or with: dolphin-ng"
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "(~/.local/bin is not in your PATH: use $COMMAND)" ;; esac
[ "$THEME" = 1 ] && [ "$APPLY" = 0 ] && echo "The theme files are installed but not active: run this again with --apply-theme, or pick Motto-Neo in Kvantum Manager, in the colour schemes and in the window decorations."
echo "Settings: Configure Dolphin > NG."
