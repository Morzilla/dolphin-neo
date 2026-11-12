#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Motto-Neo Kvantum theme from Motto-Dark-Kvantum-2026.

Usage: build.py SRC_DIR SRC_NAME BBOX_TXT OUT_DIR OUT_NAME

Keeps every element of the original SVG (arrows, indicators, shadows...) and
redraws the widget surfaces: filled tonal buttons, rounded corners, one accent
colour. BBOX_TXT is the output of `inkscape --query-all` on the source SVG.
"""
import re
import sys
from lxml import etree

SRC_DIR, SRC_NAME, BBOX, OUT_DIR, OUT_NAME = sys.argv[1:6]

SVG = "http://www.w3.org/2000/svg"
XLINK = "http://www.w3.org/1999/xlink"
G1 = "#6396c6"           # accent gradient, left  (steel blue, lighter)
G2 = "#3d6896"           # accent gradient, right (same hue, darker)
ACC = "#4f7fb0"          # solid accent between the two, for the Qt palette
ACC_HI = "#6a9bcc"
MONO_L, MONO_R = 0.30, 0.07   # neutral (grey) selection gradient: opacity of white, left and right
WHITE = "#ffffff"
BLACK = "#000000"
SURFACE = "#27292d"      # window colour: neutral dark grey
BASE = "#1f2023"         # content areas, opaque
POPUP = "#232528"
RECOLOR = {"#262a32": SURFACE, "#1e2228": "#1d1e21", "#1b1e20": "#19191b"}

# ---------------------------------------------------------------- geometry
geo = {}
for line in open(BBOX):
    q = line.strip().rsplit(",", 4)
    if len(q) == 5 and q[0]:
        geo[q[0]] = tuple(float(v) for v in q[1:])

new_nodes = []           # (id, [svg child elements])


def f(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def rect(x, y, w, h, fill, op):
    e = etree.Element(f"{{{SVG}}}rect", x=f(x), y=f(y), width=f(w), height=f(h))
    e.set("style", f"fill:{fill};fill-opacity:{op};stroke:none")
    return e


def path(d, fill, op):
    e = etree.Element(f"{{{SVG}}}path", d=d)
    e.set("style", f"fill:{fill};fill-opacity:{op};stroke:none")
    return e


def col(spec, where):
    """(colour, opacity) -> concrete paint for a slice. 'grad' is the accent gradient:
    it runs left to right, so left slices take G1, right slices G2, the rest the gradient."""
    c, op = spec
    if c == "grad":
        c = G1 if where == "left" else G2 if where == "right" else "url(#neo-h)"
    elif c == "gradd":
        c = "url(#neo-d)"
    elif c == "mono":          # neutral gradient: white fading from MONO_L to MONO_R opacity
        if where == "left":
            c, op = WHITE, op * MONO_L
        elif where == "right":
            c, op = WHITE, op * MONO_R
        else:
            c = "url(#neo-m)"
    return c, op


def pin(x, y, w, h):
    """Invisible rectangle that fixes the bounding box of a slice."""
    return rect(x, y, w, h, BLACK, 0)


def corner(box, which, fill, border, bw, inset=0.0):
    """Quarter-disc corner slice. bw = border width as a fraction of the slice."""
    x, y, w, h = box
    # arc centre is the inner corner of the slice; sx/sy point outwards
    cx = x + w if "left" in which else x
    cy = y + h if "top" in which else y
    sx = -1 if "left" in which else 1
    sy = -1 if "top" in which else 1
    sweep = 1 if sx * sy > 0 else 0
    out = [pin(x, y, w, h)]
    side = "left" if "left" in which else "right"
    fill = col(fill, side) if fill else fill
    border = col(border, side) if border else border
    rx, ry = w * (1 - inset), h * (1 - inset)
    ix, iy = w * (1 - inset - bw), h * (1 - inset - bw)
    if fill and fill[1] > 0:
        d = (f"M {f(cx)},{f(cy)} L {f(cx + sx * ix)},{f(cy)} "
             f"A {f(ix)},{f(iy)} 0 0 {sweep} {f(cx)},{f(cy + sy * iy)} Z")
        out.append(path(d, *fill))
    if border and bw > 0 and border[1] > 0:
        d = (f"M {f(cx + sx * rx)},{f(cy)} "
             f"A {f(rx)},{f(ry)} 0 0 {sweep} {f(cx)},{f(cy + sy * ry)} "
             f"L {f(cx)},{f(cy + sy * iy)} "
             f"A {f(ix)},{f(iy)} 0 0 {1 - sweep} {f(cx + sx * ix)},{f(cy)} Z")
        out.append(path(d, *border))
    return out


def edge(box, which, fill, border, bw, inset=0.0):
    x, y, w, h = box
    out = [pin(x, y, w, h)]
    fill = col(fill, which) if fill else fill
    border = col(border, which) if border else border
    k, r = inset, 1 - inset - bw          # r = share of the slice covered by the fill
    if which == "top":
        b, i = (x, y + h * k, w, h * bw), (x, y + h * (k + bw), w, h * r)
    elif which == "bottom":
        b, i = (x, y + h * r, w, h * bw), (x, y, w, h * r)
    elif which == "left":
        b, i = (x + w * k, y, w * bw, h), (x + w * (k + bw), y, w * r, h)
    else:
        b, i = (x + w * r, y, w * bw, h), (x, y, w * r, h)
    if fill and fill[1] > 0:
        out.append(rect(*i, *fill))
    if border and bw > 0 and border[1] > 0:
        out.append(rect(*b, *border))
    return out


SLICES = ("topleft", "top", "topright", "left", "right", "bottomleft", "bottom", "bottomright")


def nine(prefix, fill, border=None, bw_px=0.0, frame_px=6.0, inset_px=0.0):
    """Redraw the nine slices of `prefix` (e.g. 'button-normal').

    fill/border are (colour, opacity). The corner radius equals the frame size
    set in the .kvconfig (frame_px); bw_px is the border width in pixels.
    """
    bw = bw_px / frame_px if frame_px else 0
    inset = inset_px / frame_px if frame_px else 0
    done = 0
    for s in SLICES:
        box = geo.get(f"{prefix}-{s}")
        if not box:
            continue
        parts = corner(box, s, fill, border, bw, inset) if s in ("topleft", "topright", "bottomleft", "bottomright") \
            else edge(box, s, fill, border, bw, inset)
        new_nodes.append((f"{prefix}-{s}", parts))
        done += 1
    box = geo.get(prefix)
    if box:
        parts = [pin(*box)]
        if fill and fill[1] > 0:
            parts.append(rect(*box, *col(fill, "center")))
        new_nodes.append((prefix, parts))
        done += 1
    if not done:
        print(f"  (skipped, not in the source: {prefix})")


def single(elem_id, builder):
    box = geo.get(elem_id)
    if not box:
        print(f"  (skipped, not in the source: {elem_id})")
        return
    new_nodes.append((elem_id, [pin(*box)] + builder(*box)))


def rrect_path(x, y, w, h, r):
    return (f"M {f(x + r)},{f(y)} H {f(x + w - r)} A {f(r)},{f(r)} 0 0 1 {f(x + w)},{f(y + r)} "
            f"V {f(y + h - r)} A {f(r)},{f(r)} 0 0 1 {f(x + w - r)},{f(y + h)} "
            f"H {f(x + r)} A {f(r)},{f(r)} 0 0 1 {f(x)},{f(y + h - r)} "
            f"V {f(y + r)} A {f(r)},{f(r)} 0 0 1 {f(x + r)},{f(y)} Z")


def ring_path(x, y, w, h, r, t):
    """Rounded-rectangle outline of thickness t, drawn as a filled shape."""
    inner = rrect_path(x + t, y + t, w - 2 * t, h - 2 * t, max(r - t, 0.01))
    return rrect_path(x, y, w, h, r) + " " + inner


def evenodd(e):
    e.set("style", e.get("style") + ";fill-rule:evenodd")
    return e


# ---------------------------------------------------------------- widgets
T = lambda op: (WHITE, op)      # tonal white
D = lambda op: (BLACK, op)      # tonal black
A = lambda op=1.0: ("grad", op)      # accent gradient (9-slice)
M = lambda op=1.0: ("mono", op)       # neutral grey gradient (selections)
AD = lambda op=1.0: ("gradd", op)    # accent gradient, diagonal (single elements)

# push buttons and combo boxes: filled, borderless, 6 px radius
for fam in ("button", "combo"):
    nine(f"{fam}-normal", T(0.085), T(0.05), 1, 6)
    nine(f"{fam}-focused", T(0.14), A(0.95), 1.2, 6)
    nine(f"{fam}-pressed", T(0.05), T(0.04), 1, 6)
    nine(f"{fam}-toggled", A(), None, 0, 6)

# tool buttons: invisible until hovered
for fam in ("tbutton", "flatbutton"):
    nine(f"{fam}-normal", T(0), None, 0, 6)
    nine(f"{fam}-focused", T(0.11), None, 0, 6)
    nine(f"{fam}-pressed", T(0.06), None, 0, 6)
    nine(f"{fam}-toggled", T(0.17), None, 0, 6)

# text fields: recessed, thin outline, accent outline when focused
nine("lineedit-normal", D(0.22), T(0.11), 1, 6)
nine("lineedit-focused", D(0.26), A(), 1.5, 6)

# tabs: pill for the current tab instead of an underline
nine("tab-normal", T(0), None, 0, 6)
nine("tab-focused", T(0.07), None, 0, 6)
nine("tab-toggled", M(0.8), None, 0, 6)

# selections
nine("menuitem-pressed", M(1.0), None, 0, 5)
nine("menuitem-toggled", M(0.85), None, 0, 5)
nine("menubaritem-focused", T(0.11), None, 0, 5)
nine("menubaritem-pressed", T(0.16), None, 0, 5)
nine("menubaritem-toggled", T(0.16), None, 0, 5)
nine("itemview-focused", T(0.05), None, 0, 8, 3)
nine("itemview-pressed", M(0.6), None, 0, 8, 3)       # selected and hovered: only a little lighter         # note: in multi-column tree views the gradient restarts per cell
nine("itemview-toggled", M(0.45), None, 0, 8, 3)      # selected: enough to stand out on the dark rows, text stays readable

# no separator lines: view frames, dock frames and splitter grips are invisible
# the header stays opaque: in Dolphin the file rows scroll underneath it
nine("header-normal", (BASE, 1), None, 0, 1)
nine("header-focused", ("#34363b", 1), None, 0, 1)
nine("header-pressed", ("#3d4045", 1), None, 0, 1)
nine("header-toggled", (BASE, 1), None, 0, 1)
nine("common-normal", T(0), None, 0, 1)
nine("dock-normal", T(0), None, 0, 1)
nine("dock-focused", T(0), None, 0, 1)
nine("group-normal", T(0), T(0.09), 1, 1)
for g in ("splitter-grip-normal", "splitter-grip-focused", "splitter-grip-pressed"):
    single(g, lambda x, y, w, h: [])

# progress bar: plain groove, solid accent
nine("progress-normal", T(0.11), None, 0, 3)
nine("progress-pattern-normal", A(), None, 0, 3)
nine("progress-pattern-normal-inactive", A(0.6), None, 0, 3)
nine("progress-pattern-disabled", T(0.25), None, 0, 3)

# scrollbar handle and slider
nine("scrollbarslider-normal", T(0.24), None, 0, 5)
nine("scrollbarslider-focused", T(0.40), None, 0, 5)
nine("scrollbarslider-pressed", A(), None, 0, 5)
nine("slider-normal", T(0.16), None, 0, 3)
nine("slider-toggled", A(), None, 0, 3)


def knob(fill, ring):
    def b(x, y, w, h):
        r = min(w, h) / 2
        cx, cy = x + w / 2, y + h / 2
        out = [path(rrect_path(cx - r, cy - r, 2 * r, 2 * r, r), *fill)]
        if ring:
            out.append(evenodd(path(ring_path(cx - r, cy - r, 2 * r, 2 * r, r, r * 0.16), *col(ring, "c"))))
        return out
    return b


for pfx in ("slidercursor", "slidercursor-tickless"):
    single(f"{pfx}-normal", knob(("#e5e9f0", 1), None))
    single(f"{pfx}-focused", knob((WHITE, 1), AD()))
    single(f"{pfx}-pressed", knob((ACC_HI, 1), None))
    single(f"{pfx}-disabled", knob(("#6d7381", 1), None))

# popups: 8 px / 6 px radius, hairline outline
nine("menu-normal", (POPUP, 1), T(0.09), 1, 8)
nine("tooltip-normal", ("#1d1e21", 1), T(0.12), 1, 6)


# check boxes and radio buttons
def box_off(border):
    def b(x, y, w, h):
        r, t = w * 0.28, w * 0.1
        return [path(rrect_path(x, y, w, h, r), BLACK, 0.2),
                evenodd(path(ring_path(x, y, w, h, r, t), *border))]
    return b


def box_on(fill, mark):
    def b(x, y, w, h):
        out = [path(rrect_path(x, y, w, h, w * 0.28), *col(fill, "c"))]
        if mark == "check":
            pts = [(0.25, 0.52), (0.43, 0.70), (0.76, 0.33), (0.69, 0.26), (0.43, 0.55), (0.32, 0.45)]
            d = "M " + " L ".join(f"{f(x + px * w)},{f(y + py * h)}" for px, py in pts) + " Z"
            out.append(path(d, WHITE, 1))
        else:
            out.append(rect(x + w * 0.27, y + h * 0.44, w * 0.46, h * 0.12, WHITE, 1))
        return out
    return b


single("checkbox-normal", box_off(T(0.38)))
single("checkbox-focused", box_off((G1, 1)))
single("checkbox-checked-normal", box_on(AD(), "check"))
single("checkbox-checked-focused", box_on((ACC_HI, 1), "check"))
single("checkbox-tristate-normal", box_on(AD(), "dash"))
single("checkbox-tristate-focused", box_on((ACC_HI, 1), "dash"))


def radio_off(border):
    def b(x, y, w, h):
        r = min(w, h) / 2
        return [path(rrect_path(x, y, 2 * r, 2 * r, r), BLACK, 0.2),
                evenodd(path(ring_path(x, y, 2 * r, 2 * r, r, r * 0.2), *border))]
    return b


def radio_on(fill):
    def b(x, y, w, h):
        r = min(w, h) / 2
        d = r * 0.42
        return [path(rrect_path(x, y, 2 * r, 2 * r, r), *col(fill, "c")),
                path(rrect_path(x + r - d, y + r - d, 2 * d, 2 * d, d), WHITE, 1)]
    return b


single("radio-normal", radio_off(T(0.38)))
single("radio-focused", radio_off((G1, 1)))
single("radio-checked-normal", radio_on(AD()))
single("radio-checked-focused", radio_on((ACC_HI, 1)))

# ---------------------------------------------------------------- rewrite the SVG
parser = etree.XMLParser(remove_blank_text=False, huge_tree=True)
tree = etree.parse(f"{SRC_DIR}/{SRC_NAME}.svg", parser)
root = tree.getroot()
by_id = {e.get("id"): e for e in root.iter() if e.get("id")}
hrefs = {}
for e in root.iter(f"{{{SVG}}}use"):
    hrefs.setdefault(e.get(f"{{{XLINK}}}href", "").lstrip("#"), []).append(e)

defs = etree.SubElement(root, f"{{{SVG}}}defs", id="neo-defs")
for gid, x2, y2 in (("neo-h", "1", "0"), ("neo-d", "1", "1")):
    lg = etree.SubElement(defs, f"{{{SVG}}}linearGradient", id=gid, x1="0", y1="0", x2=x2, y2=y2)
    etree.SubElement(lg, f"{{{SVG}}}stop", offset="0").set("style", f"stop-color:{G1};stop-opacity:1")
    etree.SubElement(lg, f"{{{SVG}}}stop", offset="1").set("style", f"stop-color:{G2};stop-opacity:1")
for e in root.iter():                              # navy tint for the surfaces kept from Motto
    st = e.get("style")
    if st:
        for a, b in RECOLOR.items():
            st = re.sub(a, b, st, flags=re.I)
        e.set("style", st)
lg = etree.SubElement(defs, f"{{{SVG}}}linearGradient", id="neo-m", x1="0", y1="0", x2="1", y2="0")
etree.SubElement(lg, f"{{{SVG}}}stop", offset="0").set("style", f"stop-color:{WHITE};stop-opacity:{MONO_L}")
etree.SubElement(lg, f"{{{SVG}}}stop", offset="1").set("style", f"stop-color:{WHITE};stop-opacity:{MONO_R}")
layer = etree.SubElement(root, f"{{{SVG}}}g", id="neo-layer")
replaced = 0
for elem_id, parts in new_nodes:
    old = by_id.get(elem_id)
    if old is not None:
        if elem_id in hrefs:                     # still referenced by a <use>: keep it under another id
            old.set("id", f"orig-{elem_id}")
            for u in hrefs[elem_id]:
                u.set(f"{{{XLINK}}}href", f"#orig-{elem_id}")
        else:
            old.getparent().remove(old)
    g = etree.SubElement(layer, f"{{{SVG}}}g", id=elem_id)
    g.extend(parts)
    replaced += 1

tree.write(f"{OUT_DIR}/{OUT_NAME}.svg", xml_declaration=True, encoding="UTF-8", standalone=False)
print(f"redrawn {replaced} elements")

# ---------------------------------------------------------------- .kvconfig
cfg = open(f"{SRC_DIR}/{SRC_NAME}.kvconfig").read()


def setkeys(text, section, **kv):
    """Set keys inside the first [section]; add the ones that are missing."""
    m = re.search(rf"^\[{re.escape(section)}\]\n(.*?)(?=^\[|\Z)", text, re.S | re.M)
    if not m:
        raise SystemExit(f"section {section} not found")
    body = m.group(1)
    for k, v in kv.items():
        k = k.replace("__", ".")
        if re.search(rf"^{re.escape(k)}=.*$", body, re.M):
            body = re.sub(rf"^{re.escape(k)}=.*$", f"{k}={v}", body, flags=re.M)
        else:
            body = body.rstrip("\n") + f"\n{k}={v}\n\n"
    return text[:m.start(1)] + body + text[m.end(1):]


def frame(n):
    return dict(frame__top=n, frame__bottom=n, frame__left=n, frame__right=n)


cfg = setkeys(cfg, "GeneralColors", window__color=SURFACE, button__color=SURFACE, mid__color=SURFACE,
              mid__light__color=SURFACE, base__color=BASE,      # opaque: content areas (file lists, editors) are solid, only the chrome is translucent
               alt__base__color="#14151700",   # no stripes: alternate rows are painted over the base, so this must be fully transparent
              highlight__color=ACC, inactive__highlight__color="#3d5f84", link__color="#7aaad6",
              link__visited__color="#7f8fe6")
cfg = setkeys(cfg, "%General", comment=f"{OUT_NAME} (filled, rounded redraw of Motto-Dark by l4k1)",
              menu_blur_radius=8, tooltip_blur_radius=6, reduce_window_opacity=0, reduce_menu_opacity=30,   # windows opaque; only menus (and the patched Dolphin sidebar) are see-through
               scroll_width=10, check_size=18,
              progressbar_thickness=6, slider_width=6, slider_handle_width=18, slider_handle_length=18)
cfg = setkeys(cfg, "HeaderSection", frame__top=0, frame__bottom=0, frame__left=0, frame__right=0,
              text__margin__left=4, text__margin__right=4)
cfg = setkeys(cfg, "PanelButtonCommand", **frame(6), text__margin__top=1, text__margin__bottom=1,
              text__margin__left=4, text__margin__right=4, text__toggle__color="#ffffff",
              text__normal__color="#e5e9f0", text__press__color="#e5e9f0")
cfg = setkeys(cfg, "PanelButtonTool", **frame(6), text__toggle__color="#ffffff")   # the source had "ffffff" without the #
cfg = setkeys(cfg, "ToolbarButton", **frame(6), frame__expansion=0)
cfg = setkeys(cfg, "LineEdit", **frame(6), text__margin__left=2, text__margin__right=2)
cfg = setkeys(cfg, "IndicatorSpinBox", frame__top=6, frame__bottom=6, frame__left=6, frame__right=6)
cfg = setkeys(cfg, "ComboBox", **frame(6), text__margin__left=2, text__margin__right=2)
cfg = setkeys(cfg, "Tab", **frame(6), text__margin__left=6, text__margin__right=6,
              text__margin__top=1, text__margin__bottom=1)
cfg = setkeys(cfg, "ItemView", **frame(8), text__margin__top=0, text__margin__bottom=0,
              text__margin__left=1, text__margin__right=1)
cfg = setkeys(cfg, "MenuItem", **frame(5), text__margin__left=3, text__margin__right=3,
              text__toggle__color="#ffffff", text__focus__color="#ffffff", text__press__color="#ffffff")
cfg = setkeys(cfg, "MenuBarItem", **frame(5), text__margin__left=2, text__margin__right=2)
cfg = setkeys(cfg, "Menu", **frame(8))
cfg = setkeys(cfg, "ToolTip", **frame(6))
cfg = setkeys(cfg, "Progressbar", **frame(3), frame__expansion=0)
cfg = setkeys(cfg, "ProgressbarContents", **frame(3))
cfg = setkeys(cfg, "ScrollbarSlider", **frame(5))
cfg = setkeys(cfg, "Slider", **frame(3))
open(f"{OUT_DIR}/{OUT_NAME}.kvconfig", "w").write(cfg)
print("kvconfig written")
