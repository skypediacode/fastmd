#!/usr/bin/env python3
"""Generate uniform-font-size KaTeX-backed SVG math icons specifically for Functions.

Renders all function expressions with a fixed, uniform font size inside a wide canvas
(1920x640 viewBox, 3:1 ratio) so that expressions fit cleanly with no clipping and
matching letter sizes across all function icons.
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
OUTPUT_DIR = ROOT / "resources" / "icons" / "math" / "functions"
ICON_CONFIG = ROOT / "tools" / "math-icons" / "actions.json"

FUNCTIONS = [
    {"id": "ln", "category": "Functions", "label": "Natural logarithm", "latex": "\\ln(x)"},
    {"id": "log", "category": "Functions", "label": "Logarithm", "latex": "\\log_{b}(x)"},
    {"id": "exp", "category": "Functions", "label": "Exponential", "latex": "e^x"},
    {"id": "sin", "category": "Functions", "label": "Sine", "latex": "\\sin(x)"},
    {"id": "cos", "category": "Functions", "label": "Cosine", "latex": "\\cos(x)"},
    {"id": "tan", "category": "Functions", "label": "Tangent", "latex": "\\tan(x)"},
    {"id": "cot", "category": "Functions", "label": "Cotangent", "latex": "\\cot(x)"},
    {"id": "sec", "category": "Functions", "label": "Secant", "latex": "\\sec(x)"},
    {"id": "csc", "category": "Functions", "label": "Cosecant", "latex": "\\csc(x)"},
    {"id": "arcsin", "category": "Functions", "label": "Inverse sine", "latex": "\\arcsin(x)", "icon_latex": "\\sin^{-1}(x)"},
    {"id": "arccos", "category": "Functions", "label": "Inverse cosine", "latex": "\\arccos(x)", "icon_latex": "\\cos^{-1}(x)"},
    {"id": "arctan", "category": "Functions", "label": "Inverse tangent", "latex": "\\arctan(x)", "icon_latex": "\\tan^{-1}(x)"},
    {"id": "sinh", "category": "Functions", "label": "Hyperbolic sine", "latex": "\\sinh(x)"},
    {"id": "cosh", "category": "Functions", "label": "Hyperbolic cosine", "latex": "\\cosh(x)"},
    {"id": "tanh", "category": "Functions", "label": "Hyperbolic tangent", "latex": "\\tanh(x)"},
    {"id": "limsup", "category": "Functions", "label": "Limit superior", "latex": "\\limsup_{n\\to\\infty}a_n"},
    {"id": "liminf", "category": "Functions", "label": "Limit inferior", "latex": "\\liminf_{n\\to\\infty}a_n"},
    {"id": "max", "category": "Functions", "label": "Maximum", "latex": "\\max_x f(x)"},
    {"id": "min", "category": "Functions", "label": "Minimum", "latex": "\\min_x f(x)"},
    {"id": "sup", "category": "Functions", "label": "Supremum", "latex": "\\sup A"},
    {"id": "inf", "category": "Functions", "label": "Infimum", "latex": "\\inf A"},
    {"id": "det", "category": "Functions", "label": "Determinant", "latex": "\\det A"},
    {"id": "dim", "category": "Functions", "label": "Dimension", "latex": "\\dim V"},
    {"id": "ker", "category": "Functions", "label": "Kernel", "latex": "\\ker T"},
    {"id": "gcd", "category": "Functions", "label": "Greatest common divisor", "latex": "\\gcd(a,b)"},
    {"id": "Pr", "category": "Functions", "label": "Probability", "latex": "\\Pr(A)"},
]

SCALE_FACTOR = 10
# PNG masters are baked by generate-icon-pngs.py at this target height.
PNG_HEIGHT = 160

with ICON_CONFIG.open(encoding="utf-8") as config_file:
    ICON_CONFIG_DATA = json.load(config_file)

FUNCTION_CANVAS = ICON_CONFIG_DATA["category_canvases"]["Functions"]
WIDTH = int(FUNCTION_CANVAS["width"])
HEIGHT = int(FUNCTION_CANVAS["height"])
PAGE_W = WIDTH * SCALE_FACTOR
PAGE_H = HEIGHT * SCALE_FACTOR
FONT_SIZE = float(FUNCTION_CANVAS["fontSize"]) * SCALE_FACTOR

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

def make_html(katex_js: Path, katex_css: Path) -> str:
    action_data = []
    for a in FUNCTIONS:
        action_data.append({
            "id": a["id"],
            "latex": a.get("icon_latex", a["latex"]),
            "fontSize": FONT_SIZE
        })
    payload = json.dumps(action_data, ensure_ascii=False).replace("</", "<\\/")
    
    css = katex_css.read_text(encoding="utf-8")
    fonts_dir = (katex_css.parent / "fonts").as_posix()
    css = css.replace("url(fonts/", f"url(file:///{fonts_dir}/")
    
    html = f'''<!doctype html><meta charset="utf-8">
<style>
{css}
@page {{ size: {PAGE_W}px {PAGE_H}px; margin: 0; }}
body {{ margin: 0; background: transparent; color: #000; }}
.page {{ width: {PAGE_W}px; height: {PAGE_H}px; page-break-after: always; position: relative; overflow: hidden; }}
.icon-container {{ position: absolute; width: max-content; height: max-content; font-size: {FONT_SIZE}px; color: #000000; line-height: normal; }}
.katex-mathml {{ display: none !important; }}
</style>
<script>{katex_js.read_text(encoding="utf-8")}</script>
<body>
<script>
const actions = {payload};
const pageW = {PAGE_W};
const pageH = {PAGE_H};

for (const a of actions) {{
    const page = document.createElement('div');
    page.className = 'page';
    
    const container = document.createElement('div');
    container.className = 'icon-container';
    container.style.fontSize = a.fontSize + 'px';
    container.style.top = '0';
    container.style.left = '0';
    katex.render(a.latex, container, {{displayMode: false, throwOnError: true}});
    
    document.body.appendChild(container);
    
    const mathml = container.querySelector('.katex-mathml');
    if (mathml) mathml.style.display = 'none';
    
    const mathHtml = container.querySelector('.katex-html') || container;
    
    const range = document.createRange();
    range.selectNodeContents(mathHtml);
    const rect = range.getBoundingClientRect();
    const containerRect = container.getBoundingClientRect();
    
    const scaledW = rect.width;
    const scaledH = rect.height;
    
    // Keep the expression near the left edge so the Functions popup reads as
    // a compact, left-aligned list rather than a column of centered badges.
    const dx = 42 - (rect.left - containerRect.left);
    const dy = (pageH - scaledH) / 2 - (rect.top - containerRect.top);
    
    container.style.transformOrigin = '0 0';
    container.style.transform = 'matrix(1, 0, 0, 1, ' + dx + ', ' + dy + ')';
    
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
    # windowing of extensible/stretchy KaTeX glyphs silently no-ops in the
    # toolbar's QIcon rendering, leaking oversized unclipped tile geometry as
    # stray strokes. <mask> luminance masking gives the same visual result
    # and Qt does support it.
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

def clean_svg(raw: str, action: dict, ink_rect: fitz.Rect) -> str:
    svg = re.sub(r'<\?xml.*?\?>\s*', '', raw)
    svg = re.sub(r'<!DOCTYPE.*?>\s*', '', svg)
    
    vbox_w = PAGE_W
    vbox_h = PAGE_H
    
    new_svg_tag = f'<svg id="math-{action["id"]}" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {vbox_w} {vbox_h}" width="{WIDTH}" height="{HEIGHT}" color="currentColor">'
    svg = re.sub(r'<svg[^>]*>', new_svg_tag, svg, count=1)
    
    svg = re.sub(r'#000000', 'currentColor', svg, flags=re.IGNORECASE)
    svg = re.sub(r'#000\b', 'currentColor', svg, flags=re.IGNORECASE)
    svg = clippath_to_mask(svg)

    return svg.strip() + "\n"

def main() -> None:
    katex = ROOT / "resources" / "katex"
    html = make_html(katex / "katex.min.js", katex / "katex.min.css")
    browser = browser_path()
    
    with tempfile.TemporaryDirectory(prefix="fastmd-func-icons-") as td:
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
        if len(doc) != len(FUNCTIONS):
            doc.close()
            raise SystemExit(f"PDF page count mismatch: expected {len(FUNCTIONS)}, got {len(doc)}")
            
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        dpi_matrix = fitz.Matrix(4/3, 4/3)
        
        for i, action in enumerate(FUNCTIONS):
            page = doc[i]
            target = OUTPUT_DIR / f"{action['id']}.svg"
            
            rects = []
            for p in page.get_drawings():
                r = p["rect"] * dpi_matrix
                if "scissor" in p and p["scissor"]:
                    r = r & (p["scissor"] * dpi_matrix)
                if not r.is_empty:
                    rects.append(r)
            for b in page.get_text("dict")["blocks"]:
                if "bbox" in b:
                    r = fitz.Rect(b["bbox"]) * dpi_matrix
                    if not r.is_empty:
                        rects.append(r)
                        
            if rects:
                ink_rect = rects[0]
                for r in rects[1:]:
                    ink_rect = ink_rect | r
                if ink_rect.width > 3000:
                    ink_rect = fitz.Rect(0, 0, 0, 0)
            else:
                ink_rect = fitz.Rect(0, 0, 0, 0)
                
            raw_svg = page.get_svg_image(matrix=dpi_matrix, text_as_path=True)
            svg = clean_svg(raw_svg, action, ink_rect)
            target.write_text(svg, encoding="utf-8", newline="\n")

        doc.close()

    expected = {OUTPUT_DIR / f"{action['id']}.{extension}"
                for action in FUNCTIONS for extension in ("svg", "png")}
    expected.update(OUTPUT_DIR / f"func.{extension}" for extension in ("svg", "png"))
    for stale in list(OUTPUT_DIR.glob("*.svg")) + list(OUTPUT_DIR.glob("*.png")):
        if stale not in expected:
            stale.unlink()
        
    print(f"Successfully generated {len(FUNCTIONS)} function icons in {OUTPUT_DIR}")

if __name__ == "__main__":
    main()
