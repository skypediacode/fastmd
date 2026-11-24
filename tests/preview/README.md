# Preview incremental-render regression tests

For small documents `PreviewWidget` pushes the **whole** rendered body to the
WebView2 shell on every editor debounce, and `resources/preview/preview.js` diffs
that body against the previous push and swaps in only the top-level blocks that
actually changed, decorating just those (highlight.js, KaTeX, Mermaid, code
badges) instead of the entire document. For large documents the C++ side does
that diff itself and sends only the changed blocks as a `patch` (see
`patchtest` below).

These harnesses guard the invariant that makes that safe: **one Markdown block ==
one top-level element child of `#content`, in order** — and that an incrementally
updated DOM is indistinguishable from a from-scratch render of the same body.

Neither harness is copied into `build/preview` by CMake: that directory is the
packaged asset layout and must not contain test files. Copy them in to run, then
delete them.

## 1. `difftest` — synthetic bodies, behaviour + cost

```sh
cp tests/preview/difftest.* build/preview/
"/c/Program Files/Google/Chrome/Application/chrome.exe" \
  --headless=new --disable-gpu --allow-file-access-from-files \
  --virtual-time-budget=60000 --user-data-dir=/tmp/fastmd-difftest \
  --dump-dom "file:///$(pwd)/build/preview/difftest.html" \
  | grep -A40 DIFFTEST_BEGIN
rm build/preview/difftest.*
```

Every line should read `PASS`, ending in `DIFFTEST_VERDICT ALL PASSED`. The two
`WORK` lines report decoration cost: a one-block edit should be ~0
`hljs.highlightElement` calls and 1 KaTeX pass, against 40 and 374 for a
whole-document change (the old per-keystroke cost).

## 2. `realsamples` — every real document in `samples/`

Covers the constructs synthetic bodies miss: tables, callouts, footnotes,
definition lists, task lists, KaTeX, mhchem, Mermaid. For each document it
applies the three edit shapes the diff must handle — a changed middle block, an
appended block, a removed first block — and compares against a fresh render.

First generate the bodies (needs `PreviewPipelineBench`, built with
`-DFASTMD_BUILD_TESTS=ON`):

```sh
./build/PreviewPipelineBench.exe --emit /tmp/fastmd-bodies samples/*.md
python - <<'EOF'
import io, os, json, glob
out = []
for f in sorted(glob.glob("/tmp/fastmd-bodies/*.preview.html")):
    body = io.open(f, encoding="utf-8").read()
    if body.strip():
        out.append({"name": os.path.basename(f)[:-len(".preview.html")], "html": body})
io.open("build/preview/sampledata.js", "w", encoding="utf-8").write(
    "window.__SAMPLES = " + json.dumps(out) + ";\n")
EOF

cp tests/preview/realsamples.* build/preview/
"/c/Program Files/Google/Chrome/Application/chrome.exe" \
  --headless=new --disable-gpu --allow-file-access-from-files \
  --virtual-time-budget=180000 --user-data-dir=/tmp/fastmd-difftest \
  --dump-dom "file:///$(pwd)/build/preview/realsamples.html" \
  | grep -A20 REALSAMPLES_BEGIN
rm build/preview/realsamples.* build/preview/sampledata.js
```

Expect `REALSAMPLES_VERDICT ALL PASSED`.

### Why SVG subtrees are normalized away

Mermaid mints fresh element ids per render **and** routes edges with randomized
bezier control points, so two from-scratch renders of the same source are not
byte-equal. `realsamples.js` therefore collapses `<svg>…</svg>` to a placeholder
while still asserting the diagram *count* matches, so a dropped or duplicated
diagram still fails.

`control.js` demonstrates that nondeterminism independently of the diff: it
renders the same body twice in two fresh iframes and reports where they differ.
Run it by copying `realsamples.html` to `control.html` with the script name
swapped (`sed 's|realsamples.js|control.js|'`). Use it before assuming a
Mermaid-related `realsamples` failure is a diff bug.

## 3. `shifttest` — line-shifting edits keep their nodes

Enter, Backspace-joins and multi-line pastes move the absolute `data-src-*`
(and task-item) line numbers of every block below the edit. The diff keeps
those blocks' nodes and renumbers them in place; this harness proves it. For
each case it pushes the old body, keeps references to every top-level node,
pushes the new body, then checks that the blocks an independent oracle says are
unchanged are **the same DOM objects**, that the DOM (line attributes included)
equals a fresh render, and that double-clicking a heading below the edit posts
its real source line. Synthetic cases always run; real-document cases (edits to
paragraphs, headings, lists, quotes, tables, fences, Mermaid, math, footnotes,
front matter) need the bodies generated first:

```sh
python tests/preview/shiftcases.py build/PreviewPipelineBench.exe build/preview/shiftdata.js
cp tests/preview/shifttest.* build/preview/
"/c/Program Files/Google/Chrome/Application/chrome.exe" \
  --headless=new --disable-gpu --allow-file-access-from-files \
  --window-size=1000,800 --virtual-time-budget=300000 \
  --user-data-dir=/tmp/fastmd-shifttest \
  --dump-dom "file:///$(cygpath -m "$(pwd)")/build/preview/shifttest.html" \
  | sed -n '/SHIFTTEST_BEGIN/,/SHIFTTEST_END/p'
rm build/preview/shifttest.* build/preview/shiftdata.js
```

Expect `SHIFTTEST_VERDICT ALL PASSED`. Each line reports `retained`, `rebuilt`
and `shiftedInPlace` node counts; a local edit rebuilds only the edited block(s)
plus, where the document has them, the `[TOC]` and the footnote list.

## Harness notes

All three stub `window.chrome.webview` to impersonate the C++ host, and stub
`requestAnimationFrame` onto timers — headless Chrome stops firing rAF once the
page goes idle, which would stall `reportRendered()` and hang the run.

## Math copy compatibility and layout

`mathlayouttest` uses both exact Vietnamese underbrace inputs from the bug
report, plus overbraces, underset/overset, substack, fractions, limits and a
matrix. It compares KaTeX DOM output and measures each label below its brace,
centered and unclipped, in light/dark themes, after edits and native C++ patches.
It also checks the actual standalone HTML export and print shells.

```powershell
python tests/preview/mathlayoutcases.py build/PreviewPipelineBench.exe build/preview
Copy-Item tests/preview/mathlayouttest.* build/preview/
& 'C:\Program Files\Google\Chrome\Application\chrome.exe' `
  --headless=new --disable-gpu --allow-file-access-from-files `
  --window-size=1400,900 --virtual-time-budget=60000 `
  --user-data-dir="$env:TEMP/fastmd-mathlayouttest" --dump-dom `
  "file:///$((Get-Location).Path.Replace('\', '/'))/build/preview/mathlayouttest.html"
Remove-Item build/preview/mathlayouttest.*, build/preview/mathlayoutdata.js
```

Expect `MATHLAYOUT_VERDICT ALL PASSED`. `PreviewPipelineBench --emit-layout`
adds full standalone and print HTML to its usual three parser outputs; ordinary
`--emit` remains lightweight. Generated export pages are under `build/mathlayout`.

## Document spacing

`spacingtest` drives the real `spacing` message at the shell and measures
computed styles and layout geometry at every Line, Paragraph and List preset:
defaults, zero paragraph/list spacing, line-height 1.0 with inline code, `mark`,
`kbd` and KaTeX (nothing clipped or overlapping), the table line-height cap,
unchanged heading margins/heading-to-block gaps/code-block and math spacing,
natural margin collapsing above headings, that spacing survives later content
and theme messages, reset, and that the exported `:root` rule resolves the same
as the live message. It needs the bundled KaTeX next to `build/preview`.

```sh
cp tests/preview/spacingtest.* build/preview/
"/c/Program Files/Google/Chrome/Application/chrome.exe"   --headless=new --disable-gpu --allow-file-access-from-files   --window-size=1000,800 --virtual-time-budget=60000   --user-data-dir=/tmp/fastmd-spacingtest   --dump-dom "file:///$(cygpath -m "$(pwd)")/build/preview/spacingtest.html"   | sed -n '/SPACINGTEST_BEGIN/,/SPACINGTEST_VERDICT/p'
rm build/preview/spacingtest.*
```

Expect `SPACINGTEST_VERDICT ALL PASSED`. `DocumentSpacingTest` is the native half.

## 4. `decortest` — superseded decoration passes

Decoration is asynchronous (asset loads, Mermaid renders), so a newer push can
abandon a pass before it finishes. That push usually keeps the very blocks the
pass was decorating, since they did not change. `preview.js` carries such roots
over in `pendingRoots` until a pass that is still the newest completes. This
harness pushes A and then B before A's pass can finish (cold assets, warm
assets, mid-Mermaid render, an identical re-push, three pushes in a row) and
requires every code block highlighted and badged, every formula typeset, every
diagram drawn, and the DOM equal to a fresh render of B:

```sh
cp tests/preview/decortest.* build/preview/
"/c/Program Files/Google/Chrome/Application/chrome.exe" \
  --headless=new --disable-gpu --allow-file-access-from-files \
  --window-size=1000,800 --virtual-time-budget=120000 \
  --user-data-dir=/tmp/fastmd-decortest \
  --dump-dom "file:///$(cygpath -m "$(pwd)")/build/preview/decortest.html" \
  | sed -n '/DECORTEST_BEGIN/,/DECORTEST_END/p'
rm build/preview/decortest.*
```

Expect `DECORTEST_VERDICT ALL PASSED`.

## 5. `patchtest` — changed-block transport

Large documents are diffed in C++ (`src/PreviewPatch.*`) and only a `patch`
message crosses to WebView2. This harness applies the exact patch the C++ side
computes for every `shifttest` edit and requires: no refusal, every block the
patch does not name is the same DOM object afterwards, the DOM (line attributes
included) equals a fresh render, a heading below the edit double-clicks to its
true line, and every block of the C++ split parses on its own into the element
the browser makes of it inside the whole body. Synthetic cases cover the
protocol: stale `base`, block-count and fragment mismatches, an untrusted split,
recovery through a whole-body resync, decoration of inserted blocks only, and a
patch superseding another one's decoration pass.

```sh
python tests/preview/shiftcases.py build/PreviewPipelineBench.exe build/preview/shiftdata.js
python tests/preview/patchcases.py build/PreviewPipelineBench.exe build/preview/shiftdata.js build/preview/patchdata.js
cp tests/preview/patchtest.* build/preview/
"/c/Program Files/Google/Chrome/Application/chrome.exe"   --headless=new --disable-gpu --allow-file-access-from-files   --window-size=1000,800 --virtual-time-budget=300000   --user-data-dir=/tmp/fastmd-patchtest   --dump-dom "file:///$(cygpath -m "$(pwd)")/build/preview/patchtest.html"   | sed -n '/PATCHTEST_BEGIN/,/PATCHTEST_END/p'
rm build/preview/patchtest.* build/preview/shiftdata.js build/preview/patchdata.js
```

Expect `PATCHTEST_VERDICT ALL PASSED`. Each real case reports how many blocks
were kept, inserted and renumbered, and the patch size against the whole body.
The native half is `build/PreviewPatchTest.exe` (splitting, the diff applied
back onto the old blocks, a randomized edit fuzz, and the message encoding).
`PreviewPipelineBench --split <body.html> <out.json>` and
`--patch-cost <file.md>` help when a document unexpectedly takes the
whole-body path or a patch seems large.

### Page Break boundaries

`shiftcases.py` also emits 11 Page Break edit cases containing tightly adjacent
headings, continued unordered/ordered lists and blockquotes, fences, tables,
rules, paragraphs, task lists, and consecutive breaks. Both `shifttest` and
`patchtest` check fresh/incremental DOM equality, unchanged block identity,
source navigation after line shifts, and (for patches) C++/browser split parity.
`MarkdownCompatibilityTest` separately compares all 900 combinations of blocks
on both sides and blank-line presence against explicitly separated input, in
preview and both export render modes; it also checks parser input provenance,
CRLF, EOF, and literal markers in code.

## 6. `softbreaktest` — semantic breaks and source mappings

`MarkdownSoftBreakTest --emit build/preview/softbreakdata.js` emits 28 bodies
with independent expected HTML, covering both single-line-break settings, LF
and CRLF, EOF, blank boundaries, two-space/backslash breaks, inline HTML and
authored spans, emphasis, quotes, lists, headings, code spans and code blocks.
The native test checks raw HTML blocks separately. The browser harness checks
each body's semantic DOM and complete source-mapped DOM, a fresh render versus
an incremental replacement, unchanged suffix identity, and setting transitions.
It ignores only whitespace **between top-level elements**, which the block
diff deliberately does not keep; whitespace inside blocks is compared exactly.
Only text-only source-mapping spans are unwrapped for the semantic comparison;
authored spans and line-break elements must remain.

```powershell
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
.\build\MarkdownSoftBreakTest.exe --emit build/preview/softbreakdata.js
Copy-Item tests/preview/softbreaktest.* build/preview/
& 'C:\Program Files\Google\Chrome\Application\chrome.exe' `
  --headless=new --disable-gpu --allow-file-access-from-files `
  --virtual-time-budget=60000 --timeout=20000 `
  --user-data-dir="$env:TEMP/fastmd-softbreaktest" --dump-dom `
  "file:///$((Get-Location).Path.Replace('\', '/'))/build/preview/softbreaktest.html"
Remove-Item build/preview/softbreaktest.*, build/preview/softbreakdata.js
```

Expect `SOFTBREAKTEST_VERDICT ALL PASSED`. Generate fixtures after each native
test build; missing fixtures fail the browser harness. `shiftcases.py` also
adds seven real soft-break edits to `shifttest` and `patchtest`: text edits,
blank-line insertion, explicit hard-break transitions, quote boundaries, CRLF,
and raw HTML edits. Those check source navigation, node identity, and the C++
split/patch against the browser, with all source attributes retained.

## 6. `fonttest` — body font and the Vietnamese fallback

Drives the `font` message: the body stack, the `faces` it carries (injected as
one `style#fastmd-font-faces`, removed by a message without them), and, by
measuring glyph widths, that a fallback face really replaces the "ế" Georgia
would otherwise build from "ê" plus a spacing acute. Needs Georgia and Segoe UI
(every Windows install has both).

```sh
cp tests/preview/fonttest.* build/preview/
"/c/Program Files/Google/Chrome/Application/chrome.exe" \
  --headless=new --disable-gpu --allow-file-access-from-files \
  --virtual-time-budget=30000 --user-data-dir=/tmp/fastmd-fonttest \
  --dump-dom "file:///$(cygpath -m "$(pwd)")/build/preview/fonttest.html" \
  | sed -n '/FONTTEST_BEGIN/,/FONTTEST_END/p'
rm build/preview/fonttest.*
```

Expect `FONTTEST_VERDICT ALL PASSED`.
