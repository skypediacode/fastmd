"""Builds build/preview/patchdata.js for patchtest.html.

Takes the before/after bodies shiftcases.py rendered (shiftdata.js) and asks
the C++ side (PreviewPipelineBench --patch) for the patch the preview would
send for each edit, so patchtest applies exactly what FastMD posts.

    python tests/preview/shiftcases.py build/PreviewPipelineBench.exe build/preview/shiftdata.js
    python tests/preview/patchcases.py build/PreviewPipelineBench.exe build/preview/shiftdata.js build/preview/patchdata.js
"""
import io
import json
import os
import subprocess
import sys
import tempfile


def main():
    bench, shiftdata, out = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
    text = io.open(shiftdata, encoding="utf-8").read()
    cases = json.loads(text[text.index("["):text.rindex("]") + 1])
    tmp = tempfile.mkdtemp(prefix="fastmd-patch-")
    data = []
    for n, c in enumerate(cases):
        a, b, o = (os.path.join(tmp, "%d.%s" % (n, s)) for s in ("base.html", "next.html", "json"))
        io.open(a, "w", encoding="utf-8", newline="\n").write(c["base"])
        io.open(b, "w", encoding="utf-8", newline="\n").write(c["next"])
        subprocess.run([bench, "--patch", a, b, o], check=True, stdout=subprocess.DEVNULL)
        result = json.loads(io.open(o, encoding="utf-8").read())
        c = dict(c)
        c["blocks"] = result["blocks"]
        c["patch"] = result["patch"]
        # Where the C++ split puts each block, to hold against the browser's parse.
        for key, path in (("baseSpans", a), ("nextSpans", b)):
            subprocess.run([bench, "--split", path, o], check=True, stdout=subprocess.DEVNULL)
            c[key] = json.loads(io.open(o, encoding="utf-8").read())
        data.append(c)
    io.open(out, "w", encoding="utf-8").write("window.__PATCH_CASES = " + json.dumps(data) + ";\n")
    print("wrote %d cases (%d patched) to %s" % (len(data), sum(1 for d in data if d["patch"]), out))


if __name__ == "__main__":
    main()
