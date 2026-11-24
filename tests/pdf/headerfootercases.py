"""Check real header/footer output: FastMD's PDF export and the Print document.

    python tests/pdf/headerfootercases.py build/PdfHeaderFooterTest.exe <scratch-dir>

Needs PyMuPDF (`pip install pymupdf`). The Print half also needs Edge or Chrome;
each browser found prints the File > Print document with its own header/footer
switched ON -- the print dialog's "Headers and footers" checkbox -- to prove
FastMD's margin boxes suppress the browser's date/title/URL in every band.
"""
import os
from pathlib import Path
import subprocess
import sys

import pymupdf

sys.stdout.reconfigure(encoding="utf-8")

MM = 72 / 25.4
exe, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run([str(exe), str(out)], check=True)

failures = []


def check(condition, message):
    print(("PASS " if condition else "FAIL ") + message)
    if not condition:
        failures.append(message)


def bands(pdf, top_mm, bottom_mm, side_mm=15):
    """Per page: (top slots, bottom slots), each a sorted list of (third, text).

    Words are assigned to the left/center/right third of the content width by
    their centre, the same thirds the margin boxes occupy; PyMuPDF would
    otherwise merge boxes that share a baseline into one block.
    """
    pages = []
    for page in pymupdf.open(pdf):
        width, height = page.rect.width, page.rect.height
        left, third = side_mm * MM, (width - 2 * side_mm * MM) / 3
        top, bottom = {}, {}
        for x0, y0, x1, y1, word, *_ in sorted(page.get_text("words"), key=lambda w: (round(w[1]), w[0])):
            middle = (y0 + y1) / 2
            if middle <= top_mm * MM:
                target = top
            elif middle >= height - bottom_mm * MM:
                target = bottom
            else:
                continue
            slot = min(2, max(0, int(((x0 + x1) / 2 - left) // third)))
            target.setdefault(slot, []).append(word)
        pages.append((sorted((k, " ".join(v)) for k, v in top.items()),
                      sorted((k, " ".join(v)) for k, v in bottom.items())))
    return pages


def joined(items):
    return " | ".join(text for _, text in items)


def slots(items):
    return [k for k, _ in items]


# ---- PDF export (WebView2) -------------------------------------------------
off = bands(out / "off.pdf", 18, 18)
check(len(off) >= 3, f"fixture spans several pages ({len(off)})")
check(all(not t and not b for t, b in off), "off: no header or footer text")

header = bands(out / "header.pdf", 18, 18)
check(len(header) == len(off), "header: pagination unchanged by header/footer")
for t, b in header:
    check(joined(t) == "hf-sample.md | HDR-MID | Header Footer Fixture", f"header: three slots in order ({joined(t)})")
    check(not b, "header: footer band empty")
    check(slots(t) == [0, 1, 2], f"header: left/center/right thirds ({slots(t)})")
    break
check(all(joined(t) == joined(header[0][0]) for t, _ in header), "header: same on every page")

footer = bands(out / "footer.pdf", 18, 18)
n = len(footer)
for i, (t, b) in enumerate(footer, 1):
    check(not t, f"footer p{i}: header band empty")
    check(joined(b) == f"FTR-LEFT | Page {i} of {n} | P{i}/{n}", f"footer p{i}: counters ({joined(b)})")

both = bands(out / "both.pdf", 18, 18)
n = len(both)
for i, (t, b) in enumerate(both, 1):
    check(joined(t) == "hf-sample.md | Header Footer Fixture" and slots(t) == [0, 2]
          and joined(b) == f"Page {i} of {n}" and slots(b) == [1],
          f"both p{i}: {joined(t)} / {joined(b)}")

escape = bands(out / "escape.pdf", 18, 18)
t, b = escape[0]
# PyMuPDF splits CJK runs into several words, so compare without spaces.
check(joined(t).replace(" ", "") == 'Say "hi" \\ </style> {page} Tiếng Việt 日本語'.replace(" ", ""),
      f"escape: literal text and Unicode ({joined(t)})")
check(joined(b) == f"%1 %2 | 1/{len(escape)}", f"escape: %n stays literal ({joined(b)})")

untitled = bands(out / "untitled.pdf", 18, 18)
check(joined(untitled[0][0]) == "Untitled 7", f"untitled: tab name stands in for {{file}} ({joined(untitled[0][0])})")

narrow = bands(out / "narrow.pdf", 6, 6)
t, b = narrow[0]
check(joined(t).startswith("Long header text") and joined(b) == f"Page 1 of {len(narrow)}",
      f"narrow 6mm margins: header and footer still rendered ({joined(t)[:40]}... / {joined(b)})")
# ---- Print document through a header/footer-enabled browser ----------------
browsers = [p for p in (
    Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft/Edge/Application/msedge.exe",
    Path(os.environ.get("ProgramFiles", "")) / "Google/Chrome/Application/chrome.exe",
) if p.is_file()]
if not browsers:
    print("SKIP print checks: no Edge or Chrome")
for browser in browsers:
    tag = browser.stem
    profile = out / f"profile-{tag}"
    for case in ("off", "header", "footer", "both"):
        pdf = out / f"print_{case}_{tag}.pdf"
        subprocess.run([str(browser), "--headless=new", "--disable-gpu", "--allow-file-access-from-files",
                        f"--user-data-dir={profile}", f"--print-to-pdf={pdf}", "--virtual-time-budget=10000",
                        (out / f"print_{case}.html").as_uri()],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pages = bands(pdf, 18, 18)
        text = " ".join(joined(t) + " " + joined(b) for t, b in pages)
        browser_header = "BROWSERTITLE" in text or "file:///" in text
        if case == "off":
            check(browser_header, f"print/{tag}/off: browser header/footer unchanged when disabled")
        else:
            check(not browser_header, f"print/{tag}/{case}: no browser-generated header/footer")
        if case in ("footer", "both"):
            last = len(pages)
            check(joined(pages[-1][1]).endswith(f"{last} of {last}") or f"Page {last} of {last}" in joined(pages[-1][1]),
                  f"print/{tag}/{case}: counters on the last page ({joined(pages[-1][1])})")
        if case in ("header", "both"):
            check(joined(pages[0][0]).startswith("hf-sample.md"), f"print/{tag}/{case}: header text ({joined(pages[0][0])})")

print(f"\n{'ALL PASSED' if not failures else str(len(failures)) + ' FAILED'}")
sys.exit(1 if failures else 0)
