#!/usr/bin/env python3
"""Generate monochrome KaTeX-backed SVG math toolbar assets.

The catalog is the only input: add an action to tools/math-icons/actions.json,
then run this script. Chrome executes KaTeX locally, then PyMuPDF extracts the
rendered vector paths from the generated PDF. The final SVGs are fully 
self-contained with no font dependencies or <text> elements.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

# Require PyMuPDF for vector extraction
try:
    import fitz
except ImportError:
    raise SystemExit("PyMuPDF (fitz) is required. Run: pip install PyMuPDF")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "tools" / "math-icons" / "actions.json"
DEFAULT_OUTPUT = ROOT / "resources" / "icons" / "math"
DEFAULT_CHEMISTRY_CONFIG = ROOT / "tools" / "math-icons" / "chemistry.json"
SCALE_FACTOR = 10
# PNG masters are baked by generate-icon-pngs.py after this SVG generator.
# Keep this shared target here so all math/chemistry generators document the
# asset contract in one place without changing the SVG canvas.
PNG_HEIGHT = 160

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

def canvas_for(action: dict, config: dict) -> dict:
    c = action.get("canvas") or config.get("category_canvases", {}).get(action["category"]) or config["canvas"]
    c = dict(c)
    # Chemistry is rendered onto a deliberately generous *measurement* page.
    # Its exported SVG is subsequently sized from the real KaTeX ink bounds;
    # this page is never its final canvas and must not constrain long equations.
    if action["category"] == "Chemistry":
        c["width"] = int(c["renderWidth"])
    return c

def make_page(actions: list[dict], katex_js: Path, katex_css: Path, mhchem_js: Path, config: dict) -> str:
    cat_canvases = config.get("category_canvases", {})
    default_canvas = config["canvas"]
    font_size = float(default_canvas["fontSize"])

    action_data = []
    page_names: dict[tuple[int, int], str] = {}
    for a in actions:
        c = canvas_for(a, config)
        w, h = int(c["width"]), int(c["height"])
        page_name = page_names.setdefault((w, h), f"canvas_{w}x{h}")
        action_data.append({
            "id": a["id"],
            "latex": a.get("icon_latex", a["latex"]),
            "width": w,
            "height": h,
            "pageName": page_name,
        })
    payload = json.dumps(action_data, ensure_ascii=False).replace("</", "<\\/")

    # Chrome's headless --print-to-pdf otherwise always paginates onto the
    # default US Letter paper and shrink-to-fits any page wider/taller than
    # that (e.g. the 192x64 Functions canvas), silently downscaling those
    # icons relative to square ones. A named @page per distinct canvas size
    # forces Chrome to size each PDF page exactly, so no page is ever
    # rescaled to fit a mismatched paper size.
    page_rules = "\n".join(
        f"@page {name} {{ size: {w * SCALE_FACTOR}px {h * SCALE_FACTOR}px; margin: 0; }}\n"
        f".{name} {{ page: {name}; }}"
        for (w, h), name in page_names.items()
    )

    # We load CSS directly into <style> and replace url(fonts/) with absolute file:/// paths
    css = katex_css.read_text(encoding="utf-8")
    fonts_dir = (katex_css.parent / "fonts").as_posix()
    css = css.replace("url(fonts/", f"url(file:///{fonts_dir}/")
    
    html = f'''<!doctype html><meta charset="utf-8">
<style>
{css}
{page_rules}
body {{ margin: 0; background: transparent; color: #000; }}
.page {{ page-break-after: always; position: relative; overflow: hidden; }}
.icon-container {{ position: absolute; width: max-content; height: max-content; color: #000000; line-height: normal; }}
.katex-mathml {{ display: none !important; }}
</style>
<script>{katex_js.read_text(encoding="utf-8")}</script>
<script>{mhchem_js.read_text(encoding="utf-8")}</script>
<body>
<script>
const actions = {payload};
const scaleFactor = {SCALE_FACTOR};
const fontSize = {font_size:g};

for (const a of actions) {{
    const pageW = a.width * scaleFactor;
    const pageH = a.height * scaleFactor;
    const page = document.createElement('div');
    page.className = 'page ' + a.pageName;
    page.style.width = pageW + 'px';
    page.style.height = pageH + 'px';

    const container = document.createElement('div');
    container.className = 'icon-container';
    // Every notation renders at the same font size, unscaled to fit its
    // canvas. The measured ink bounding box is centered afterward in Python.
    container.style.fontSize = (fontSize * scaleFactor) + 'px';
    container.style.top = '0';
    container.style.left = '0';
    katex.render(a.latex, container, {{displayMode: false, throwOnError: true}});
    
    // Hide mathml just to be absolutely sure it doesn't affect bounds.
    const mathml = container.querySelector('.katex-mathml');
    if (mathml) mathml.style.display = 'none';
    
    page.appendChild(container);
    document.body.appendChild(page);
}}
</script>
</body></html>'''
    return html

_MASK_RECT_PATH_RE = re.compile(
    r'<path[^>]*\btransform="matrix\(([^)]+)\)"[^>]*\bd="M([\d.eE+-]+) ([\d.eE+-]+)H'
    r'([\d.eE+-]+)V([\d.eE+-]+)H[\d.eE+-]+Z"')

def _mask_region(inner: str) -> str:
    # Every clip path PyMuPDF emits here is a plain axis-aligned rect (an
    # "MxyHxVyHxZ" box) under a pure scale+translate matrix (no rotation/
    # skew), so its actual on-canvas bounds can be computed directly instead
    # of guessing. Using that tight, geometry-derived rect (instead of a
    # blanket huge region) keeps the offscreen mask buffer Qt rasterizes
    # small -- see the comment on clippath_to_mask for why size matters here.
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
    # QtSvg (used by IconHelper::generatedMathSvgIcon/QIcon to rasterize these
    # assets for toolbar buttons and popups) does not support <clipPath> at
    # all -- Qt's own docs say so explicitly. PyMuPDF's PDF->SVG conversion
    # relies on <clipPath> to window KaTeX's extensible/stretchy glyphs (wide
    # accents, stretchy chemistry arrows, tall delimiters), which are exported
    # as oversized tiled path geometry that only looks right once clipped. In
    # a real browser this clips fine; in Qt it silently no-ops and the full
    # oversized tile bleeds through as stray strokes/hatching. <mask> luminance
    # masking produces the same visual result and Qt does support it, so swap
    # every <clipPath>/clip-path pair for an equivalent <mask>/mask pair.
    #
    # The mask's own x/y/width/height region matters, not just its content:
    # Qt's QSvgMask::createMask() rasterizes that region into a real QImage
    # (see QtSvg's private qsvgstructure_p.h). An earlier version of this
    # function used a blanket "-1000%/3000%" region (in userSpaceOnUse units,
    # percentages resolve against the ambient viewport, not the small clip
    # rect) to guarantee nothing was ever cropped -- but that meant every
    # masked icon rasterized an offscreen buffer tens of thousands of units
    # across, at every requested icon size, which is what made popups with
    # many icons (e.g. the Chemistry "Misc" popup) visibly stutter on first
    # open. _mask_region() computes the actual small rect instead.
    def repl(m: re.Match) -> str:
        clip_id, inner = m.group(1), m.group(2)
        if 'fill=' not in inner:
            inner = re.sub(r'(<path\b[^>]*?)(/?>)', r'\1 fill="#ffffff"\2', inner, count=1)
        region = _mask_region(inner)
        return f'<mask id="{clip_id}" maskUnits="userSpaceOnUse" {region}>{inner}</mask>'
    svg = re.sub(r'<clipPath id="([^"]+)">(.*?)</clipPath>', repl, svg, flags=re.S)
    svg = re.sub(r'\bclip-path="url\(#', 'mask="url(#', svg)
    return svg

def clean_svg(raw: str, action: dict, config: dict, ink_rect: fitz.Rect) -> tuple[str, int]:
    c = canvas_for(action, config)
    height = int(c["height"])
    padding = int(c.get("padding", 4))
    is_chemistry = action["category"] == "Chemistry"
    if ink_rect.width <= 0 or ink_rect.height <= 0:
        raise RuntimeError(f"KaTeX produced no visible ink for {action['id']}")

    if is_chemistry:
        # The final Chemistry canvas is derived solely from its rendered ink, plus
        # a small fixed safety margin. Do not quantize it to a list of widths: that
        # would either waste space or clip a formula that grows past the last slot.
        width = max(1, int((ink_rect.width + SCALE_FACTOR - 1) // SCALE_FACTOR) + 2 * padding)
    else:
        # Every other category renders onto its own fixed square (or
        # category-specific) canvas -- KaTeX's font size for these is already
        # tuned so ink fits inside it, so the canvas is only ever centered
        # around the ink, never resized to it.
        width = int(c["width"])

    # Remove XML headers
    svg = re.sub(r'<\?xml.*?\?>\s*', '', raw)
    svg = re.sub(r'<!DOCTYPE.*?>\s*', '', svg)

    # PyMuPDF outputs viewBox for the page
    vbox_w = width * SCALE_FACTOR
    vbox_h = height * SCALE_FACTOR

    # Chemistry's canvas width is exactly the measured ink plus padding, so
    # stray sub-pixel overflow has nowhere to go; every other category has a
    # generous fixed canvas around its ink, so hidden overflow is harmless
    # and keeps stray oversized tile geometry (see clippath_to_mask) from
    # ever peeking past the icon's own box.
    overflow = "visible" if is_chemistry else "hidden"
    new_svg_tag = f'<svg id="math-{action["id"]}" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {vbox_w} {vbox_h}" width="{width}" height="{height}" color="currentColor" overflow="{overflow}">'
    svg = re.sub(r'<svg[^>]*>', new_svg_tag, svg, count=1)

    # Convert absolute blacks from PyMuPDF into currentColor
    svg = re.sub(r'#000000', 'currentColor', svg, flags=re.IGNORECASE)
    svg = re.sub(r'#000\b', 'currentColor', svg, flags=re.IGNORECASE)
    svg = clippath_to_mask(svg)

    if is_chemistry:
        # Horizontal padding protects arrowheads and glyph strokes; vertical
        # centering keeps every Chemistry button aligned on the existing
        # 64-unit icon height.
        dx = padding * SCALE_FACTOR - ink_rect.x0
        dy = (vbox_h - ink_rect.height) / 2 - ink_rect.y0
    else:
        # Plain centering on the fixed canvas -- never resize or rescale.
        dx = (vbox_w - ink_rect.width) / 2 - ink_rect.x0
        dy = (vbox_h - ink_rect.height) / 2 - ink_rect.y0
    svg = re.sub(r'(<svg[^>]*>)', rf'\1\n<g transform="translate({dx:g} {dy:g})">', svg, count=1)
    svg = svg.replace("</svg>", "</g>\n</svg>")

    return svg.strip() + "\n", width

def write_audit(path: Path, actions: list[dict]) -> None:
    groups: dict[str, list[dict]] = {}
    for action in actions:
        groups.setdefault(action["category"], []).append(action)
    lines = [
        "# Math toolbar action audit", "",
        "The Math Toolbar exposes 11 direct quick-access buttons and 13 ordered popup groups.",
        f"**Catalog size:** {len(actions)} actions. Every row has one SVG at the path recorded in `resources/icons/math/mapping.json`.", ""
    ]
    for category, entries in groups.items():
        lines += [f"## {category}", "", "| Action | LaTeX expression |", "|---|---|"]
        lines += [f"| `{a['id']}` — {a['label']} | `{a['latex']}` |" for a in entries]
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--chemistry-config", type=Path, default=DEFAULT_CHEMISTRY_CONFIG)
    ap.add_argument("--audit", type=Path, default=ROOT / "docs" / "MATH_TOOLBAR_AUDIT.md")
    ap.add_argument("--ids", nargs="+", help="Generate only these action ids")
    args = ap.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))
    chemistry_config = json.loads(args.chemistry_config.read_text(encoding="utf-8"))
    config.setdefault("category_canvases", {})["Chemistry"] = chemistry_config["canvas"]
    config["actions"].extend(chemistry_config["actions"])
    # The generic `func` toolbar button is part of the main catalog. The
    # remaining Functions popup assets use their dedicated fixed-font generator,
    # and every Matrices asset uses its own dedicated normalized-scale generator
    # (see generate-function-icons.py, generate-matrices-icons.py) -- exclude
    # both here so no icon is ever produced by two different scripts. Every
    # other category (Chemistry included) is generated by this script.
    actions = [
        a for a in config["actions"]
        if (a["category"] != "Functions" or a["id"] == "func")
        and a["category"] != "Matrices"
    ]

    if args.ids:
        requested = set(args.ids)
        actions = [action for action in actions if action["id"] in requested]
        missing = requested - {action["id"] for action in actions}
        if missing:
            raise SystemExit(f"Unknown math icon id(s): {', '.join(sorted(missing))}")

    ids = [a["id"] for a in actions]
    if len(ids) != len(set(ids)):
        raise SystemExit("Duplicate action id in catalog")

    katex = ROOT / "resources" / "katex"
    page = make_page(actions, katex / "katex.min.js", katex / "katex.min.css", katex / "contrib" / "mhchem.min.js", config)
    browser = browser_path()

    with tempfile.TemporaryDirectory(prefix="fastmd-math-icons-") as td:
        page_path = Path(td) / "render.html"
        pdf_path = Path(td) / "render.pdf"
        page_path.write_text(page, encoding="utf-8")

        # We need virtual-time-budget so Chrome waits for the fonts to load
        cmd = [
            browser, "--headless", "--disable-gpu", "--no-sandbox",
            "--allow-file-access-from-files", "--virtual-time-budget=10000",
            f"--print-to-pdf={pdf_path}", f"file:///{page_path.as_posix()}"
        ]
        subprocess.run(cmd, check=True, capture_output=True, text=True, encoding="utf-8", errors="replace")

        if not pdf_path.exists():
            raise SystemExit("Chrome failed to generate PDF")

        doc = fitz.open(pdf_path)
        if len(doc) != len(actions):
            doc.close()
            raise SystemExit(f"PDF page count mismatch: expected {len(actions)}, got {len(doc)}")

        args.output.mkdir(parents=True, exist_ok=True)
        mapping = {"schema": 1, "canvas": config["canvas"], "category_canvases": config.get("category_canvases", {}), "icons": {}}
        expected = set()

        for i, action in enumerate(actions):
            page = doc[i]
            category = re.sub(r"[^a-z0-9]+", "-", action["category"].lower()).strip("-")
            folder = args.output / category
            folder.mkdir(exist_ok=True)
            target = folder / f"{action['id']}.svg"
            expected.add(target)
            # Scale from 72dpi (PDF) to 96dpi (HTML pixels) so it maps to our 640/960 viewBox perfectly
            dpi_matrix = fitz.Matrix(4/3, 4/3)

            # Use rendered pixels because PDF text boxes include KaTeX layout space.
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
                ink_rect = fitz.Rect(first_x, first_y, last_x + 1, last_y + 1)
            else:
                ink_rect = fitz.Rect(0, 0, 0, 0)

            # Only Chemistry renders onto a deliberately oversized *measurement*
            # page (see canvas_for) where ink reaching the edge means it may
            # already have been clipped before the SVG canvas is calculated.
            # Every other category's page IS its final fixed canvas, so ink
            # legitimately extending toward its edge is normal, not an error.
            if action["category"] == "Chemistry" and (ink_rect.x0 <= 0 or ink_rect.x1 >= pixmap.width):
                raise RuntimeError(
                    f"KaTeX ink for {action['id']} reached the measurement page edge; "
                    "increase Chemistry.canvas.renderWidth")

            raw_svg = page.get_svg_image(matrix=dpi_matrix, text_as_path=True)
            svg, final_width = clean_svg(raw_svg, action, config, ink_rect)
            target.write_text(svg, encoding="utf-8", newline="\n")

            mapping["icons"][action["id"]] = {
                "category": action["category"],
                "expression": action.get("icon_latex", action["latex"]),
                "insertion": action["latex"],
                "file": target.relative_to(ROOT).as_posix()
                , "width": final_width
            }

        doc.close()

    # functions/ and matrices/ are owned by their own dedicated generators
    # (generate-function-icons.py, generate-matrices-icons.py); this script
    # only ever contributes func.svg to functions/ and nothing to matrices/.
    # matrices.svg and misc-launcher.svg are standalone hand-authored assets
    # at the tree root, not produced by any generator.
    if not args.ids:
        for stale in args.output.rglob("*.svg"):
            if stale.parent.name == "functions" and stale.name != "func.svg":
                continue
            if stale.parent.name == "matrices":
                continue
            if stale.parent == args.output and stale.name in ("matrices.svg", "misc-launcher.svg", "reaction-launcher.svg", "state-launcher.svg"):
                continue
            if stale not in expected:
                stale.unlink()
        (args.output / "mapping.json").write_text(json.dumps(mapping, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Generated {len(actions)} icons in {args.output}")

if __name__ == "__main__":
    main()
