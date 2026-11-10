# Motto-Neo theme

The look dolphin-neo was designed with. Optional: the fork works with any Qt style and colour scheme, light or dark.

- `kvantum/Motto-Neo/` — a Kvantum widget theme: filled, rounded controls on a neutral dark grey, with a steel-blue accent.
- `aurorae/Motto-Neo-Aurorae/` — the matching window decoration.
- `color-schemes/MottoNeo.colors` — the matching KDE colour scheme.

## Origin and licence

These are derived from the **Motto** themes by **L4ki** (l4k1):
<https://github.com/L4ki/Motto-Plasma-Themes>, published under **GPL-3.0**. The Kvantum theme is generated from
Motto-Dark-Kvantum by `kvantum/generator/build.py`, which keeps the original SVG and redraws the widget
surfaces; the decoration is a recoloured copy of Motto-Dark-Blur-Aurorae-6. The files in this folder are
therefore under GPL-3.0 as well, unlike the rest of the repository (Dolphin, GPL-2.0-or-later).

## Rebuilding the Kvantum theme

`build.py` needs the original theme as its source:

```sh
python3 kvantum/generator/build.py /path/to/Motto-Dark-Kvantum Motto-Dark-Kvantum \
        kvantum/generator/bbox.txt kvantum/Motto-Neo Motto-Neo
```

`bbox.txt` is the output of `inkscape --query-all` on the source SVG. Colours and radii are constants at the
top of the script.
