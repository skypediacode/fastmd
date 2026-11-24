#!/usr/bin/env python3
"""Generate natural-width KaTeX SVGs for the Integration popup.

Run this, then generate-icon-pngs.py.  The popup uses the PNG twins at
runtime; SVGs remain the checked-in source assets.
"""
from __future__ import annotations

import importlib.util
import argparse
import json
import math
import re
import subprocess
import tempfile
from pathlib import Path

try:
    import fitz
except ImportError:
    raise SystemExit("PyMuPDF (fitz) is required. Run: pip install PyMuPDF")

ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "resources" / "icons" / "math" / "calculus"
SCALE_FACTOR = 10
# PNG masters are baked by generate-icon-pngs.py at this target height.
PNG_HEIGHT = 160
HEIGHT = 64
FONT_SIZE = 26
MEASURE_WIDTH = 240
HORIZONTAL_PADDING = 4

SOURCE_PATH = ROOT / "src" / "MathSymbols.cpp"
ENTRY_RE = re.compile(
    r'\{\s*QStringLiteral\("(?P<id>[^"]+)"\),\s*'
    r'QStringLiteral\("Integration"\),\s*'
    r'QStringLiteral\("(?P<insert>(?:\\\\.|[^"\\])*)"\),\s*'
    r'QStringLiteral\("(?P<preview>(?:\\\\.|[^"\\])*)"\),\s*'
    r'QStringLiteral\("(?P<asset>[^"]+)"\),\s*'
    r'QString\(\),\s*1\.0,\s*QStringLiteral\("(?P<tooltip>[^"]+)"\)\s*\}')


def unescape_cpp(text: str) -> str:
    return text.replace(r"\\", "\\").replace(r'\"', '"')


def strip_math_delimiters(latex: str) -> str:
    value = latex.strip()
    if value.startswith("$$") and value.endswith("$$"):
        return value[2:-2].strip()
    if value.startswith("$") and value.endswith("$"):
        return value[1:-1].strip()
    return value


def integration_commands() -> list[dict]:
    source = SOURCE_PATH.read_text(encoding="utf-8")
    if "mathPopupGroups()" in source:
        source = source[source.find("mathPopupGroups()"):]
    commands = []
    for match in ENTRY_RE.finditer(source):
        command = {key: unescape_cpp(value) for key, value in match.groupdict().items()}
        command["latex"] = strip_math_delimiters(command["preview"])
        asset_rel = command["asset"].replace(":/icons/math/", "")
        command["output_file"] = str(ROOT / "resources" / "icons" / "math" / Path(asset_rel).with_suffix(".svg"))
        commands.append(command)
    if not commands:
        raise RuntimeError("Could not read Integration popup catalog")
    return commands


def load_math_generator():
    path = ROOT / "scripts" / "generate-math-icons.py"
    spec = importlib.util.spec_from_file_location("fastmd_math_icons", path)
    if not spec or not spec.loader:
        raise SystemExit("Could not load shared math icon helpers")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_html(actions: list[dict], katex_js: Path, katex_css: Path) -> str:
    css = katex_css.read_text(encoding="utf-8")
    fonts_dir = (katex_css.parent / "fonts").as_posix()
    css = css.replace("url(fonts/", f"url(file:///{fonts_dir}/")
    payload = json.dumps(actions, ensure_ascii=False).replace("</", "<\\/")
    page_w = MEASURE_WIDTH * SCALE_FACTOR
    page_h = HEIGHT * SCALE_FACTOR
    return f'''<!doctype html><meta charset="utf-8">
<style>
{css}
@page {{ size: {page_w}px {page_h}px; margin: 0; }}
body {{ margin: 0; background: transparent; color: #000; }}
.page {{ width: {page_w}px; height: {page_h}px; position: relative; page-break-after: always; overflow: hidden; }}
.icon-container {{ position: absolute; width: max-content; height: max-content; font-size: {FONT_SIZE * SCALE_FACTOR}px; color: #000; line-height: normal; }}
.katex-mathml {{ display: none !important; }}
</style>
<script>{katex_js.read_text(encoding="utf-8")}</script>
<body><script>
for (const action of {payload}) {{
  const page = document.createElement('div');
  page.className = 'page';
  const formula = document.createElement('div');
  formula.className = 'icon-container';
  katex.render(action.latex, formula, {{ displayMode: false, throwOnError: true }});
  page.appendChild(formula);
  document.body.appendChild(page);
}}
</script></body>'''


def ink_bounds(page: fitz.Page) -> fitz.Rect:
    pix = page.get_pixmap(matrix=fitz.Matrix(4 / 3, 4 / 3), colorspace=fitz.csGRAY, alpha=False)
    first_x, first_y, last_x, last_y = pix.width, pix.height, -1, -1
    for y in range(pix.height):
        row = pix.samples[y * pix.width:(y + 1) * pix.width]
        match = re.search(rb"[\x00-\xf4]", row)
        if match:
            first_x = min(first_x, match.start())
            first_y = min(first_y, y)
            last_x = max(last_x, pix.width - re.search(rb"[\x00-\xf4]", row[::-1]).start() - 1)
            last_y = y
    if last_x < 0:
        raise RuntimeError("KaTeX produced no visible ink")
    if first_x == 0 or last_x == pix.width - 1:
        raise RuntimeError("Calculus formula reached the measurement page edge")
    return fitz.Rect(first_x, first_y, last_x + 1, last_y + 1)


def clean_svg(raw: str, action: dict, ink: fitz.Rect, math_generator) -> str:
    width = max(1, math.ceil(ink.width / SCALE_FACTOR) + 2 * HORIZONTAL_PADDING)
    view_w = width * SCALE_FACTOR
    view_h = HEIGHT * SCALE_FACTOR
    svg = re.sub(r'<\?xml.*?\?>\s*', '', raw)
    svg = re.sub(r'<!DOCTYPE.*?>\s*', '', svg)
    root = (f'<svg id="math-{action["id"]}" xmlns="http://www.w3.org/2000/svg" '
            f'xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 {view_w} {view_h}" '
            f'width="{width}" height="{HEIGHT}" color="currentColor" overflow="hidden">')
    svg = re.sub(r'<svg[^>]*>', root, svg, count=1)
    svg = re.sub(r'#000000', 'currentColor', svg, flags=re.IGNORECASE)
    svg = re.sub(r'#000\b', 'currentColor', svg, flags=re.IGNORECASE)
    svg = math_generator.clippath_to_mask(svg)
    dx = HORIZONTAL_PADDING * SCALE_FACTOR - ink.x0
    dy = (view_h - ink.height) / 2 - ink.y0
    svg = re.sub(r'(<svg[^>]*>)', rf'\1\n<g transform="translate({dx:g} {dy:g})">', svg, count=1)
    return svg.replace('</svg>', '</g>\n</svg>').strip() + '\n'


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ids", nargs="+", help="Generate only these Integration icon ids")
    args = parser.parse_args()
    math_generator = load_math_generator()
    commands = integration_commands()
    if args.ids:
        requested = set(args.ids)
        commands = [command for command in commands if command["id"] in requested]
        missing = requested - {command["id"] for command in commands}
        if missing:
            raise SystemExit(f"Unknown Integration icon id(s): {', '.join(sorted(missing))}")
    katex = ROOT / "resources" / "katex"
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fastmd-calculus-icons-") as td:
        html_path = Path(td) / "render.html"
        pdf_path = Path(td) / "render.pdf"
        html_path.write_text(make_html(commands, katex / "katex.min.js", katex / "katex.min.css"), encoding="utf-8")
        subprocess.run([
            math_generator.browser_path(), "--headless", "--disable-gpu", "--no-sandbox",
            "--allow-file-access-from-files", "--virtual-time-budget=10000",
            f"--print-to-pdf={pdf_path}", f"file:///{html_path.as_posix()}",
        ], check=True, capture_output=True, text=True, encoding="utf-8", errors="replace")
        doc = fitz.open(pdf_path)
        if len(doc) != len(commands):
            raise RuntimeError(f"PDF page count mismatch: expected {len(commands)}, got {len(doc)}")
        for index, action in enumerate(commands):
            page = doc[index]
            ink = ink_bounds(page)
            raw = page.get_svg_image(matrix=fitz.Matrix(4 / 3, 4 / 3), text_as_path=True)
            out_file = Path(action["output_file"])
            out_file.parent.mkdir(parents=True, exist_ok=True)
            out_file.write_text(
                clean_svg(raw, action, ink, math_generator), encoding="utf-8", newline="\n")
        doc.close()
    print(f"Successfully generated {len(commands)} Integration icons")


if __name__ == '__main__':
    main()
