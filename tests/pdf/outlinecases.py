"""Check PDF bookmarks (the document outline) in FastMD's real PDF export.

    python tests/pdf/outlinecases.py build/PdfOutlineTest.exe <scratch-dir>

Needs PyMuPDF (`pip install pymupdf`). PdfOutlineTest exports each fixture
through the WebView2 DevTools print (webview), WebView2 PrintToPdf -- the path
used when the DevTools print is unavailable (printtopdf) -- and the headless
Chrome/Edge fallback (browser).
"""
from pathlib import Path
import re
import subprocess
import sys

import pymupdf

sys.stdout.reconfigure(encoding="utf-8")

exe, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run([str(exe), str(out)], check=True)

failures = []


def check(condition, message):
    print(("PASS " if condition else "FAIL ") + message)
    if not condition:
        failures.append(message)


def norm(title):
    """Chromium titles are the laid-out text: CSS text-transform applies and a
    wrapped heading loses the space at each line break, so compare without
    whitespace or case."""
    return re.sub(r"\s+", "", title).casefold()


def outline(pdf):
    with pymupdf.open(pdf) as doc:
        return [(level, title, page, dest["to"].y) for level, title, page, dest in doc.get_toc(simple=False)]


def destinations_on_headings(pdf, entries, tag):
    """Each bookmark must land on its own heading: the destination point sits
    inside the rendered heading text, and duplicate titles on one page resolve
    to distinct occurrences in document order."""
    with pymupdf.open(pdf) as doc:
        used = {}
        for _, title, page, y in entries:
            words = title.split()[:3]
            hits = [r for r in doc[page - 1].search_for(" ".join(words)) if r.y0 - 2 <= y <= r.y1]
            key = (page, norm(" ".join(words)))
            hit = next((r for r in hits if (r.y0, r.x0) not in used.get(key, set())), None)
            check(hit is not None, f"{tag}: '{title[:40]}' lands on its heading on page {page} (y={y:.1f})")
            if hit is not None:
                used.setdefault(key, set()).add((hit.y0, hit.x0))
        for page in {e[2] for e in entries}:
            ys = [e[3] for e in entries if e[2] == page]
            check(ys == sorted(ys) and len(set(ys)) == len(ys),
                  f"{tag}: page {page} bookmarks follow vertical order ({len(ys)} on the page)")


def same_rendering(a, b, tag):
    """Asking for the outline must not change pagination or layout. The
    DevTools print is Chromium's headless pipeline, which rounds page margins
    to whole CSS pixels where PrintToPdf does not: glyphs may sit up to ~0.7pt
    apart (12 mm margin = 34.02pt vs 33.75pt), so positions get 1pt of slack."""
    with pymupdf.open(a) as da, pymupdf.open(b) as db:
        check(da.page_count == db.page_count, f"{tag}: page count {da.page_count} == {db.page_count}")
        worst = 0.0
        for i, (pa, pb) in enumerate(zip(da, db), 1):
            if pa.rect != pb.rect:
                check(False, f"{tag} p{i}: page size {pa.rect} == {pb.rect}")
                return
            wa, wb = pa.get_text("words"), pb.get_text("words")
            if [w[4] for w in wa] != [w[4] for w in wb]:
                check(False, f"{tag} p{i}: same words on the page")
                return
            for x, y in zip(wa, wb):
                worst = max(worst, abs(x[0] - y[0]), abs(x[1] - y[1]), abs(x[2] - y[2]), abs(x[3] - y[3]))
        check(worst < 1.0, f"{tag}: same page size and words on every page, max offset {worst:.2f}pt")


def structure(pdf, tag):
    data = pdf.read_bytes()
    with pymupdf.open(pdf) as doc:
        check(not doc.is_repaired, f"{tag}: opens without repair")
        meta = doc.metadata
    last = data.rindex(b"startxref")
    offset = int(data[last + 9:data.index(b"%%EOF", last)].strip())
    check(data[offset:offset + 4] == b"xref", f"{tag}: final startxref points at a classic xref table")
    return data, meta


# (level, title, page) in document order. Skipped levels nest under the nearest
# shallower heading; code, front matter and HTML comments contribute nothing.
expected = [
    (1, "Intro & overview", 1),
    (2, "Skipped level", 1),
    (2, "Tiếng Việt: Định nghĩa và ví dụ", 1),
    (2, "Duplicate", 1),
    (2, "Duplicate", 1),
    (2, "Setext heading", 1),
    (2, 'Special (chars) & 1 < 2 C:\\path "quotes"', 1),
    (2, "Inline emphasis and code", 1),
    (2, "A long heading that keeps going well past the width of any bookmarks pane, "
        "so a viewer has to truncate or wrap it somewhere along the way", 1),
    (1, "Second page", 2),
    (2, "Deep H4", 2),
    (3, "Deeper H5", 2),
    (4, "Deepest H6", 2),
    (2, "Back to H2", 2),
    (2, "Quoted heading", 2),
    (3, "Callout heading", 2),
    (2, "List heading", 2),
    (2, "Area", 2),  # KaTeX math in a heading is not part of the title
]

for variant in ("webview", "browser"):
    pdf = out / f"{variant}_outline.pdf"
    data, meta = structure(pdf, f"{variant}/outline")
    entries = outline(pdf)
    got = [(level, norm(title), page) for level, title, page, _ in entries]
    want = [(level, norm(title), page) for level, title, page in expected]
    check(got == want, f"{variant}/outline: levels, titles and pages match the headings")
    if got != want:
        for row in entries:
            print("    got", row[:3])
    check(not any("notaheading" in norm(t) for _, t, *_ in entries), f"{variant}/outline: no code/comment headings")
    destinations_on_headings(pdf, entries, f"{variant}/outline")
    check(meta.get("title") == "Outline Fixture" and meta.get("author") == "Nguyễn Văn A",
          f"{variant}/outline: front matter metadata survives ({meta.get('title')} / {meta.get('author')})")
    check(data.count(b"startxref") == 2, f"{variant}/outline: metadata appended as one incremental update")

    landscape = out / f"{variant}_landscape.pdf"
    structure(landscape, f"{variant}/landscape")
    entries = outline(landscape)
    # Each page after the first opens with a heading that pagination moved
    # there, the case where Chromium used to repeat the title.
    titles = [t for _, t, *_ in entries]
    check(titles == ["Long document"] + [f"Section {i}" for i in range(1, 31)],
          f"{variant}/landscape: every heading bookmarked once, in order ({len(titles)})")
    if titles != ["Long document"] + [f"Section {i}" for i in range(1, 31)]:
        print("    got", titles)
    destinations_on_headings(landscape, entries, f"{variant}/landscape")
    with pymupdf.open(landscape) as doc:
        check(doc.page_count > 1 and doc[0].rect.width > doc[0].rect.height,
              f"{variant}/landscape: landscape, {doc.page_count} pages")
        check(len({page for _, _, page, _ in entries}) == doc.page_count,
              f"{variant}/landscape: bookmarks on every page")

    plain = out / f"{variant}_plain.pdf"
    data, _ = structure(plain, f"{variant}/plain")
    check(not outline(plain) and b"/Outlines" not in data, f"{variant}/plain: no headings, no outline")

for case in ("outline", "plain", "landscape"):
    same_rendering(out / f"webview_{case}.pdf", out / f"printtopdf_{case}.pdf", f"webview vs printtopdf/{case}")
    check(not outline(out / f"printtopdf_{case}.pdf"), f"printtopdf/{case}: exports without bookmarks")
    structure(out / f"printtopdf_{case}.pdf", f"printtopdf/{case}")

# A document without headings never asks for the outline, so it keeps the
# untagged PrintToPdf output it always had.
check(b"/StructTreeRoot" not in (out / "webview_plain.pdf").read_bytes(), "webview/plain: untagged, as before")

print(f"\n{'ALL PASSED' if not failures else str(len(failures)) + ' FAILED'}")
sys.exit(1 if failures else 0)
