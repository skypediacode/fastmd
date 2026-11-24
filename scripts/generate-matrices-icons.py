#!/usr/bin/env python3
"""Generate monochrome KaTeX-backed SVG icons for FastMD's matrix types.

Companion to generate-math-icons.py (same KaTeX-render -> Chrome PDF ->
PyMuPDF vector-extraction pipeline, output conventions, and font handling),
scoped to the fixed set of matrix notations used by the Math Toolbar's
Matrices button and the Insert Matrix dialog's type buttons. Run this instead
of generate-math-icons.py for these ids -- matrix ids are excluded there so
neither script regenerates the other's assets.

Unlike a naive per-icon "scale to fill the canvas" approach (which makes the
same "a"/"b"/"c"/"d" glyphs a visibly different size in every icon, since a
tall vmatrix bar or Bmatrix brace would get scaled down relative to a bare
matrix), every icon here is rendered with exactly the same KaTeX font size.
Only the delimiters make some icons wider or taller; the letters read as one
consistent size across the whole set. That shared font size is chosen
automatically in a first calibration pass, sized so the single largest
variant's ink fits the fixed 64x64 canvas with a safety margin -- everything
smaller than that renders proportionally smaller, with empty canvas space
around it, rather than being enlarged to match.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

try:
    import fitz
except ImportError:
    raise SystemExit("PyMuPDF (fitz) is required. Run: pip install PyMuPDF")

ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "resources" / "icons" / "math" / "matrices"

# The complete matrix-type catalog exposed by the Math Toolbar's Matrices
# button and MatrixInsertDialog's type buttons. `id` is both the filename
# and the id IconHelper/MatrixInsertDialog look up. Every variant shares the
# same sample matrix (a/b/c/d) so the rendered letter size is directly
# comparable across icons.
MATRICES = [
    {"id": "pmatrix",       "label": "Parenthesized matrix",   "latex": "\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}"},
    {"id": "bmatrix",       "label": "Bracketed matrix",       "latex": "\\begin{bmatrix}a&b\\\\c&d\\end{bmatrix}"},
    {"id": "brace-matrix",  "label": "Braced matrix",          "latex": "\\begin{Bmatrix}a&b\\\\c&d\\end{Bmatrix}"},
    {"id": "vmatrix",       "label": "Determinant matrix",     "latex": "\\begin{vmatrix}a&b\\\\c&d\\end{vmatrix}"},
    {"id": "norm-matrix",   "label": "Norm matrix",            "latex": "\\begin{Vmatrix}a&b\\\\c&d\\end{Vmatrix}"},
    {"id": "cases",         "label": "Piecewise cases",        "latex": "\\begin{cases}a&x>0\\\\b&x\\le0\\end{cases}",
     "icon_latex": "\\begin{cases}a&b\\\\c&d\\end{cases}"},
    {"id": "smallmatrix",   "label": "Small matrix (inline)",  "latex": "\\begin{smallmatrix}a&b\\\\c&d\\end{smallmatrix}"},
    {"id": "rcases",        "label": "Right-braced cases",     "latex": "\\begin{rcases}a&x>0\\\\b&x\\le0\\end{rcases}",
     "icon_latex": "\\begin{rcases}a&b\\\\c&d\\end{rcases}"},
]

CANVAS = 64          # square canvas, matches the rest of the Math Toolbar catalog
SCALE_FACTOR = 10    # render at 10x for crisp vector extraction
# PNG masters are baked by generate-icon-pngs.py at this target height.
PNG_HEIGHT = 160
PAGE_PX = CANVAS * SCALE_FACTOR

# Pass 1 (calibration) render size: deliberately tiny so nothing in this
# catalog can possibly overflow the page and get silently clipped by its
# overflow:hidden before Python ever measures it. KaTeX/CSS font-size
# scaling is linear, so measuring here and scaling up analytically (see
# main()) is equivalent to -- but far safer than -- guessing a large size
# and hoping nothing overflows.
REFERENCE_FONT_SIZE = 8

# Target ink extent (out of CANVAS) for the single LARGEST rendered variant's
# longer axis; every other icon renders proportionally smaller at the same
# shared font size. Leaves a symmetric ~5px margin on the biggest icon so
# italic overhangs, brackets, braces, and single/double bars never touch the
# edge; smaller icons naturally get more breathing room, which is expected.
FILL_PX = 54
FILL_VBOX = FILL_PX * SCALE_FACTOR


def browser_path() -> str:
    candidates = [
        os.environ.get("FASTMD_CHROME"),
        shutil.which("chrome.exe"), shutil.which("msedge.exe"),
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return str(candidate)
    raise SystemExit("Chrome/Edge not found; set FASTMD_CHROME to its executable path")


def make_page(katex_js: Path, katex_css: Path, font_size: float) -> str:
    action_data = [{"id": a["id"], "latex": a.get("icon_latex", a["latex"])} for a in MATRICES]
    payload = json.dumps(action_data, ensure_ascii=False).replace("</", "<\\/")

    css = katex_css.read_text(encoding="utf-8")
    fonts_dir = (katex_css.parent / "fonts").as_posix()
    css = css.replace("url(fonts/", f"url(file:///{fonts_dir}/")

    html = f'''<!doctype html><meta charset="utf-8">
<style>
{css}
@page {{ size: {PAGE_PX}px {PAGE_PX}px; margin: 0; }}
body {{ margin: 0; background: transparent; color: #000; }}
.page {{ width: {PAGE_PX}px; height: {PAGE_PX}px; page-break-after: always; position: relative; overflow: hidden; }}
.icon-container {{ position: absolute; top: 0; left: 0; width: max-content; height: max-content; font-size: {font_size * SCALE_FACTOR:g}px; color: #000000; line-height: normal; }}
.katex-mathml {{ display: none !important; }}
</style>
<script>{katex_js.read_text(encoding="utf-8")}</script>
<body>
<script>
const actions = {payload};
for (const a of actions) {{
    const page = document.createElement('div');
    page.className = 'page';
    const container = document.createElement('div');
    container.className = 'icon-container';
    katex.render(a.latex, container, {{displayMode: false, throwOnError: true}});
    const mathml = container.querySelector('.katex-mathml');
    if (mathml) mathml.style.display = 'none';
    page.appendChild(container);
    document.body.appendChild(page);
}}
</script>
</body></html>'''
    return html


def measure_ink(page, dpi_matrix) -> fitz.Rect:
    # Use rendered pixels rather than PDF text boxes -- KaTeX layout boxes
    # include whitespace the actual glyph ink does not.
    pixmap = page.get_pixmap(matrix=dpi_matrix, colorspace=fitz.csGRAY, alpha=False)
    first_x = pixmap.width
    first_y = pixmap.height
    last_x = last_y = -1
    for y in range(pixmap.height):
        row = pixmap.samples[y * pixmap.width:(y + 1) * pixmap.width]
        match = re.search(rb"[\x00-\xf4]", row)
        if match:
            x0 = match.start()
            x1 = pixmap.width - re.search(rb"[\x00-\xf4]", row[::-1]).start() - 1
            first_x = min(first_x, x0)
            first_y = min(first_y, y)
            last_x = max(last_x, x1)
            last_y = y
    if last_x >= 0:
        return fitz.Rect(first_x, first_y, last_x + 1, last_y + 1)
    return fitz.Rect(0, 0, 0, 0)


def render_pass(browser: str, katex_js: Path, katex_css: Path, font_size: float, need_svg: bool):
    """Render every MATRICES entry at one shared font_size and return, per
    icon in catalog order, (ink_rect, raw_svg_or_None)."""
    html = make_page(katex_js, katex_css, font_size)

    with tempfile.TemporaryDirectory(prefix="fastmd-matrix-icons-") as td:
        page_path = Path(td) / "render.html"
        pdf_path = Path(td) / "render.pdf"
        page_path.write_text(html, encoding="utf-8")

        cmd = [
            browser, "--headless", "--disable-gpu", "--no-sandbox",
            "--allow-file-access-from-files", "--virtual-time-budget=10000",
            f"--print-to-pdf={pdf_path}", f"file:///{page_path.as_posix()}"
        ]
        subprocess.run(cmd, check=True, capture_output=True, text=True, encoding="utf-8", errors="replace")

        if not pdf_path.exists():
            raise SystemExit("Chrome failed to generate PDF")

        doc = fitz.open(pdf_path)
        if len(doc) != len(MATRICES):
            doc.close()
            raise SystemExit(f"PDF page count mismatch: expected {len(MATRICES)}, got {len(doc)}")

        dpi_matrix = fitz.Matrix(4 / 3, 4 / 3)
        results = []
        for i in range(len(MATRICES)):
            page = doc[i]
            ink_rect = measure_ink(page, dpi_matrix)
            raw_svg = page.get_svg_image(matrix=dpi_matrix, text_as_path=True) if need_svg else None
            results.append((ink_rect, raw_svg))
        doc.close()

    return results


_MASK_RECT_PATH_RE = re.compile(
    r'<path[^>]*\btransform="matrix\(([^)]+)\)"[^>]*\bd="M([\d.eE+-]+) ([\d.eE+-]+)H'
    r'([\d.eE+-]+)V([\d.eE+-]+)H[\d.eE+-]+Z"')

def _mask_region(inner: str) -> str:
    # Every clip path PyMuPDF emits here is a plain axis-aligned rect under a
    # pure scale+translate matrix, so its on-canvas bounds can be computed
    # directly. A tight, geometry-derived region keeps the offscreen mask
    # buffer Qt rasterizes small -- see clippath_to_mask for why that matters.
    m = _MASK_RECT_PATH_RE.search(inner)
    if not m:
        return 'x="-1000%" y="-1000%" width="3000%" height="3000%"'
    a, _b, _c, d, e, f = (float(v) for v in m.group(1).split(','))
    x0, y0, x1, y1 = (float(v) for v in m.group(2, 3, 4, 5))
    px0, px1 = a * x0 + e, a * x1 + e
    py0, py1 = d * y0 + f, d * y1 + f
    xmin, xmax = sorted((px0, px1))
    ymin, ymax = sorted((py0, py1))
    pad = 8  # covers stroke width / antialiasing bleed at the rect's edge
    return (f'x="{xmin - pad:g}" y="{ymin - pad:g}" '
            f'width="{(xmax - xmin) + 2 * pad:g}" height="{(ymax - ymin) + 2 * pad:g}"')

def clippath_to_mask(svg: str) -> str:
    # QtSvg does not support <clipPath> at all, so PyMuPDF's clip-path-based
    # windowing of extensible/stretchy KaTeX glyphs (e.g. tall vmatrix/norm
    # bars) silently no-ops in the toolbar's QIcon rendering, leaking the
    # oversized unclipped tile geometry as stray strokes. <mask> luminance
    # masking gives the same visual result and Qt does support it.
    #
    # The mask's x/y/width/height region matters too: Qt's QSvgMask rasterizes
    # that region into a real QImage. A blanket "-1000%/3000%" region (in
    # userSpaceOnUse units, percentages resolve against the ambient viewport)
    # made every masked icon allocate a buffer tens of thousands of units
    # across at every requested size -- visible as stutter in popups with
    # many icons. _mask_region() computes the actual small rect instead.
    def repl(m: re.Match) -> str:
        clip_id, inner = m.group(1), m.group(2)
        if 'fill=' not in inner:
            inner = re.sub(r'(<path\b[^>]*?)(/?>)', r'\1 fill="#ffffff"\2', inner, count=1)
        region = _mask_region(inner)
        return f'<mask id="{clip_id}" maskUnits="userSpaceOnUse" {region}>{inner}</mask>'
    svg = re.sub(r'<clipPath id="([^"]+)">(.*?)</clipPath>', repl, svg, flags=re.S)
    svg = re.sub(r'\bclip-path="url\(#', 'mask="url(#', svg)
    return svg

def clean_svg(raw: str, action_id: str, ink_rect: fitz.Rect) -> str:
    svg = re.sub(r'<\?xml.*?\?>\s*', '', raw)
    svg = re.sub(r'<!DOCTYPE.*?>\s*', '', svg)

    new_svg_tag = (
        f'<svg id="math-{action_id}" xmlns="http://www.w3.org/2000/svg" '
        f'xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {PAGE_PX} {PAGE_PX}" '
        f'width="{CANVAS}" height="{CANVAS}" color="currentColor" overflow="hidden">'
    )
    svg = re.sub(r'<svg[^>]*>', new_svg_tag, svg, count=1)

    svg = re.sub(r'#000000', 'currentColor', svg, flags=re.IGNORECASE)
    svg = re.sub(r'#000\b', 'currentColor', svg, flags=re.IGNORECASE)
    svg = clippath_to_mask(svg)

    if ink_rect.width > 0 and ink_rect.height > 0:
        # Center only -- translate, never scale. Every icon already shares
        # the same font size (see main()), so re-scaling here would undo
        # exactly the consistency this script exists to produce.
        tx = PAGE_PX / 2 - (ink_rect.x0 + ink_rect.width / 2)
        ty = PAGE_PX / 2 - (ink_rect.y0 + ink_rect.height / 2)
        wrap = f'<g transform="translate({tx:g} {ty:g})">'
        svg = re.sub(r'(<svg[^>]*>)', rf'\1\n{wrap}', svg, count=1)
        svg = svg.replace("</svg>", "</g>\n</svg>")

    return svg.strip() + "\n"


def main() -> None:
    katex = ROOT / "resources" / "katex"
    katex_js = katex / "katex.min.js"
    katex_css = katex / "katex.min.css"
    browser = browser_path()

    # Pass 1 -- calibrate: measure every icon's ink at the shared reference
    # size to find the single largest rendered variant.
    calibration = render_pass(browser, katex_js, katex_css, REFERENCE_FONT_SIZE, need_svg=False)
    measured_dims = [max(r.width, r.height) for r, _ in calibration if r.width > 0 and r.height > 0]
    if not measured_dims:
        raise SystemExit("Calibration pass produced no measurable ink")
    max_ref_dim = max(measured_dims)

    # One global font size, applied identically to every icon: scale up from
    # the reference render just enough that the largest variant's longer ink
    # dimension reaches FILL_VBOX. Everything else renders proportionally
    # smaller at this same font size -- never independently scaled.
    global_font_size = REFERENCE_FONT_SIZE * (FILL_VBOX / max_ref_dim)

    # Pass 2 -- final: every icon rendered at that one shared font size.
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    final = render_pass(browser, katex_js, katex_css, global_font_size, need_svg=True)

    expected = set()

    for action, (ink_rect, raw_svg) in zip(MATRICES, final):
        target = OUTPUT_DIR / f"{action['id']}.svg"
        expected.add(target)

        # Sanity check: at a correctly calibrated shared font size nothing
        # should reach the page's far edge. If it does, fail loudly instead
        # of silently shipping a clipped icon.
        edge_margin = 2  # px, in the PAGE_PX-scale raster
        if ink_rect.x1 >= PAGE_PX - edge_margin or ink_rect.y1 >= PAGE_PX - edge_margin:
            raise SystemExit(
                f"'{action['id']}' ink touches the render page's far edge "
                f"({ink_rect}) at the shared font size ({global_font_size:.2f}px) "
                f"-- lower FILL_PX and retry."
            )

        svg = clean_svg(raw_svg, action["id"], ink_rect)
        target.write_text(svg, encoding="utf-8", newline="\n")

    expected_png = {t.with_suffix(".png") for t in expected}
    for stale in list(OUTPUT_DIR.glob("*.svg")) + list(OUTPUT_DIR.glob("*.png")):
        if stale not in expected and stale not in expected_png:
            stale.unlink()

    print(f"Generated {len(MATRICES)} matrix icons in {OUTPUT_DIR} "
          f"(shared KaTeX font size {global_font_size:.2f}px)")


if __name__ == "__main__":
    main()
