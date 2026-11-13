# Motto-Neo theme

The look Dolphin NG was first designed with. Optional: the fork works with any Qt style and colour scheme, light or dark.

- [`kvantum/Motto-Neo/`](kvantum/Motto-Neo/) — a Kvantum widget theme: filled, rounded controls on a neutral dark grey, with a steel-blue accent.
- [`aurorae/Motto-Neo-Aurorae/`](aurorae/Motto-Neo-Aurorae/) — the matching window decoration.
- [`color-schemes/MottoNeo.colors`](color-schemes/MottoNeo.colors) — the matching KDE colour scheme.

## Origin and licence

These are derived from the **Motto** themes by **L4ki** (l4k1):
<https://github.com/L4ki/Motto-Plasma-Themes>, published under **GPL-3.0**. The Kvantum theme is generated from
Motto-Dark-Kvantum by [`kvantum/generator/build.py`](kvantum/generator/build.py), which keeps the original SVG and redraws the widget
surfaces; the decoration is a recoloured copy of Motto-Dark-Blur-Aurorae-6. The files in this folder are
therefore under GPL-3.0 as well, unlike the rest of the repository (Dolphin, GPL-2.0-or-later).

## Rebuilding the Kvantum theme

`build.py` needs the original theme as its source (the paths are those of the sources the installer keeps in
`~/.local/src/dolphin-ng`):

```sh
python3 ~/.local/src/dolphin-ng/ng/theme/kvantum/generator/build.py /path/to/Motto-Dark-Kvantum Motto-Dark-Kvantum \
        ~/.local/src/dolphin-ng/ng/theme/kvantum/generator/bbox.txt ~/.local/src/dolphin-ng/ng/theme/kvantum/Motto-Neo Motto-Neo
```

`bbox.txt` is the output of `inkscape --query-all` on the source SVG. Colours and radii are constants at the
top of the script.
