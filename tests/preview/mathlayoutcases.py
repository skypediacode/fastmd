"""Generate real parser, C++ patch, HTML export and print fixtures.

python tests/preview/mathlayoutcases.py build/PreviewPipelineBench.exe build/preview
"""
import json
from pathlib import Path
import subprocess
import sys

bench, target = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
target.mkdir(parents=True, exist_ok=True)
fixture = Path(__file__).with_name("mathlayout.md").resolve()
work = target.parent / "mathlayout"
work.mkdir(exist_ok=True)
source = fixture.read_text(encoding="utf-8")
edited = work / "edited.md"
edited.write_text(source.replace("y-y_0", "y+y_0"), encoding="utf-8")
subprocess.run([str(bench), "--emit-layout", str(work), str(fixture), str(edited)], check=True)
base = work / (fixture.name + ".preview.html")
next_body = work / "edited.md.preview.html"
patch = work / "patch.json"
subprocess.run([str(bench), "--patch", str(base), str(next_body), str(patch)], check=True)
parsed = json.loads(patch.read_text(encoding="utf-8"))
assert parsed["patch"] is not None
exports = {}
for name in ["standalone", "print"]:
    original = work / (fixture.name + "." + name + ".html")
    headless = work / (name + "-layout.html")
    # Headless Chromium can stop firing rAF in an idle iframe. Change only the
    # paint notification scheduling, as the existing preview harnesses do.
    html = original.read_text(encoding="utf-8").replace("<head>", "<head><script>"
        "window.requestAnimationFrame=fn=>setTimeout(()=>fn(performance.now()),0);"
        "window.cancelAnimationFrame=clearTimeout;</script>", 1)
    headless.write_text(html, encoding="utf-8")
    exports[name] = "../mathlayout/" + headless.name
data = {"base": base.read_text(encoding="utf-8"),
        "edited": next_body.read_text(encoding="utf-8"),
        "patch": parsed["patch"], "count": parsed["blocks"][0], "exports": exports}
(target / "mathlayoutdata.js").write_text("window.__MATH_LAYOUT = " + json.dumps(data) + ";\n",
                                        encoding="utf-8")
