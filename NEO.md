# dolphin-neo

An experimental fork of [Dolphin](https://apps.kde.org/dolphin/), KDE's file manager, that tries a more
modern file list. It is based on Dolphin 26.08.1 and changes only how things are shown and clicked; file
operations are Dolphin's own.

This is a personal experiment, not a KDE project, and it is not meant to replace your distribution's Dolphin:
it installs into your home directory next to it.

| List, cards | List, open rows |
|---|---|
| ![list with cards](neo/screenshots/list-cards.png) | ![list with open rows](neo/screenshots/list-open.png) |

| Preview card | Icons view |
|---|---|
| ![preview card](neo/screenshots/preview-card.png) | ![icons view](neo/screenshots/icons-tiles.png) |

## What is different

**Details view**
- Each item takes two lines: the name, then the other visible columns joined; the date sits at the right edge.
  There is no column header, and rows never get wider than the window.
- No icons. The file type is a large, faint abbreviation at the start of the row, next to a faint type icon,
  and the row is washed with the main colour of that icon. Four other type marks pair a line glyph with the
  abbreviation (tile, pill, bare signs, coloured header).
- The size has its own column with a bar on a logarithmic scale (1 KiB to 10 GiB).
- Hovering a row replaces the date with three buttons: open, preview, and the full context menu.
- The preview is a card under the row: the first lines of text files, the first entries of folders, a thumbnail
  for the rest. Pictures and the first page of PDFs can be zoomed with the wheel and dragged.
- A bar above the list sorts with one click (one button per visible column, click again to reverse), filters
  by kind of file from a menu with counts, and switches between two row styles.
- Narrow windows drop the date first, then the size column.

**Icons view**
- Every item is a dark rounded tile; previews have rounded corners.

**Sidebar**
- Places is a floating "glass" panel. By default it shows a blurred copy of the desktop wallpaper, placed by
  the window's position on screen; optionally a slow "lava lamp" animation in the wallpaper's colours, or the
  compositor's real blur.
- Section titles are small pills, the internal "Devices" section is replaced by a disk usage box.

**Smaller things**
- A "Root" toolbar action.
- The location bar leaves edit mode when the focus moves to another part of the window.

## Install

One script downloads the source, installs the build dependencies, builds, and installs into your home
directory (`~/.local/dolphin-neo`), next to your distribution's Dolphin:

```sh
curl -fsSL https://raw.githubusercontent.com/Morzilla/dolphin-neo/neo/neo/install.sh | bash
```

or, from a clone, `neo/install.sh`. It adds a "Dolphin Neo" entry to the application menu and a `dolphin-neo`
command, and installs the files of the optional [Motto-Neo theme](neo/theme/README.md) without activating it
(`--apply-theme` does that). `--no-deps`, `--no-theme`, `--prefix DIR` and `--uninstall` are the other options.

The script knows dnf (Fedora), apt (Debian, Ubuntu), pacman (Arch) and zypper (openSUSE). Only the Fedora path
has been run, and only without its dependency step; reports and fixes for the others are welcome. The fork is
based on Dolphin 26.08.1, which needs KDE Frameworks 6.23 and Qt 6.4 or newer, so distributions that ship older
ones cannot build it.

To build by hand: install Dolphin's build dependencies, then `neo/build.sh` and `neo/dolphin-neo --new-window`.

Settings are in Dolphin's own settings dialog, on a page called **Neo** (stored in `dolphinrc`, group
`DolphinNeo`). Every setting can also be forced with a `DOLPHIN_NEO_*` environment variable, documented in
[`neo/dolphin-neo.conf.example`](neo/dolphin-neo.conf.example); the launcher reads those from
`~/.config/dolphin-neo.conf`, and a variable that is set wins over the dialog. `neo/dolphinui.rc.example` is a
toolbar layout with the New Folder, Up and Root buttons; the install script puts it in place if you have none.

## State of things

- Developed on X11 with KWin, Plasma 6, a dark colour scheme and the Kvantum style. The fork's own painting
  adapts to light colour schemes (checked once with Breeze Light); Wayland is untested. The "live" glass mode uses X11 calls.
- The kind-of-file names in the filter menu and the labels in the preview card are hardcoded in Italian;
  they should go through i18n.
- File kinds and their abbreviations come from the file extension, not from the MIME type.
- The filter menu is skipped in folders with more than 4000 items.
- Most settings on the Neo page are read at start-up; only the sidebar picture and the row style apply at once.
- All changes are marked with `dolphin-neo` comments in the source.

## Contributing

Issues and pull requests are welcome. Good first steps: the i18n of the hardcoded strings, live
settings, Wayland and light-theme testing, and content summaries on the second line (pages, dimensions, row
counts) from KFileMetaData.

## Licence

Like Dolphin: GPL-2.0-or-later. See `LICENSES/`. The theme in `neo/theme/` derives from L4ki's Motto themes and is
GPL-3.0; see its README.
