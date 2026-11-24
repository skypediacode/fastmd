#!/usr/bin/env python3
"""Bake a master PNG next to every icon SVG under resources/icons/math/.

Run this after the SVG generators (generate-math-icons.py,
generate-function-icons.py, generate-matrices-icons.py) so every icon --
whichever generator produced it, or if it was hand-authored -- gets a
same-basename PNG twin. IconHelper::generatedMathSvgIcon() prefers that PNG
over live-rendering the SVG through QtSvg at runtime; re-parsing, re-masking,
and re-rendering these vector dumps 7 times per icon (once per requested
toolbar/popup size), every time an icon isn't yet in its color cache, is what
made popups with many icons visibly stutter on first open. Only the alpha
channel of the PNG matters -- IconHelper discards its RGB and recolors it per
theme -- so one master PNG serves every icon regardless of which script (or
hand) produced its SVG.

Each SVG is rendered through headless Chrome at its own declared canvas size
(the width/height attributes on its <svg> root), full-bleed with no separate
crop/centering math to keep in sync with any generator: the PNG's aspect
ratio is trivially exact because it IS the SVG's own canvas, rendered once
and rasterized whole.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import argparse
from pathlib import Path

try:
    import fitz
except ImportError:
    raise SystemExit("PyMuPDF (fitz) is required. Run: pip install PyMuPDF")

ROOT = Path(__file__).resolve().parents[1]
ICON_ROOT = ROOT / "resources" / "icons" / "math"
# Keep the checked-in raster masters compact while retaining enough source
# resolution for the toolbar, popup, and HiDPI QIcon sizes.
PNG_HEIGHT = 160
SCALE_FACTOR = 10

_WIDTH_RE = re.compile(r'\bwidth="([0-9.]+)"')
_HEIGHT_RE = re.compile(r'\bheight="([0-9.]+)"')


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


def svg_size(svg_path: Path) -> tuple[int, int] | None:
    # Only the root <svg> tag's own width/height matter (same attributes
    # IconHelper::svgIntrinsicAspectRatio reads at runtime), so restrict the
    # search to roughly the opening tag rather than the whole file -- nested
    # elements (e.g. mask rects) also carry width/height attributes.
    head = svg_path.read_text(encoding="utf-8")[:1024]
    root_tag = head[:head.find(">") + 1] if ">" in head else head
    mw = _WIDTH_RE.search(root_tag)
    mh = _HEIGHT_RE.search(root_tag)
    if not mw or not mh:
        return None
    w, h = float(mw.group(1)), float(mh.group(1))
    if w <= 0 or h <= 0:
        return None
    return int(round(w)), int(round(h))


def make_page(icons: list[tuple[Path, int, int]]) -> str:
    page_names: dict[tuple[int, int], str] = {}
    entries = []
    for svg_path, w, h in icons:
        page_name = page_names.setdefault((w, h), f"canvas_{w}x{h}")
        entries.append((svg_path, w, h, page_name))

    # A named @page per distinct canvas size, same trick as the other
    # generators: Chrome's headless --print-to-pdf otherwise paginates onto
    # default US Letter paper and shrink-to-fits any larger page.
    page_rules = "\n".join(
        f"@page {name} {{ size: {w * SCALE_FACTOR}px {h * SCALE_FACTOR}px; margin: 0; }}\n"
        f".{name} {{ page: {name}; }}"
        for (w, h), name in page_names.items()
    )

    # Each icon is its own <img>-loaded SVG document, not inlined -- inlining
    # would collide, since many of these SVGs reuse mask ids like "clip_0"
    # across files. An <img> keeps every icon's mask/defs scoped to its own
    # document.
    pages_html = "\n".join(
        f'<div class="page {name}" style="width:{w * SCALE_FACTOR}px;height:{h * SCALE_FACTOR}px;">'
        f'<img src="file:///{svg_path.as_posix()}" style="width:100%;height:100%;display:block;">'
        f'</div>'
        for svg_path, w, h, name in entries
    )

    return f'''<!doctype html><meta charset="utf-8">
<style>
{page_rules}
body {{ margin: 0; background: transparent; }}
.page {{ page-break-after: always; position: relative; overflow: hidden; }}
</style>
<body>
{pages_html}
</body></html>'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ids", nargs="+", help="Bake only these icon ids")
    args = parser.parse_args()
    svg_paths = sorted(ICON_ROOT.rglob("*.svg"))
    if args.ids:
        requested = set(args.ids)
        svg_paths = [path for path in svg_paths if path.stem in requested]
        missing = requested - {path.stem for path in svg_paths}
        if missing:
            raise SystemExit(f"Unknown icon id(s): {', '.join(sorted(missing))}")
    icons: list[tuple[Path, int, int]] = []
    skipped: list[Path] = []
    for svg_path in svg_paths:
        size = svg_size(svg_path)
        if size is None:
            skipped.append(svg_path)
            continue
        icons.append((svg_path, size[0], size[1]))

    if skipped:
        names = ", ".join(p.relative_to(ROOT).as_posix() for p in skipped)
        print(f"Skipping {len(skipped)} SVG(s) with no root width/height: {names}")

    if not icons:
        raise SystemExit(f"No icon SVGs found under {ICON_ROOT}")

    browser = browser_path()
    html = make_page(icons)

    with tempfile.TemporaryDirectory(prefix="fastmd-icon-pngs-") as td:
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
        if len(doc) != len(icons):
            doc.close()
            raise SystemExit(f"PDF page count mismatch: expected {len(icons)}, got {len(doc)}")

        # No clip, no crop: each page IS the icon's own canvas (see
        # make_page), so the full rasterized page is exactly the PNG we want.
        for i, (svg_path, _w, svg_height) in enumerate(icons):
            page = doc[i]
            # The existing 4/3 matrix produces a raster height of
            # svg_height * SCALE_FACTOR. Render the same full canvas at the
            # compact target height so aspect ratio and transparency remain
            # unchanged.
            scale = PNG_HEIGHT / (svg_height * SCALE_FACTOR * 0.75)
            pix = page.get_pixmap(matrix=fitz.Matrix(scale, scale), alpha=True)
            pix.save(str(svg_path.with_suffix(".png")))

        doc.close()

    print(f"Baked {len(icons)} master PNGs under {ICON_ROOT}")


if __name__ == "__main__":
    main()
