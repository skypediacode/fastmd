"""Builds build/preview/shiftdata.js for shifttest.html.

Applies line-shifting and structural edits to real sample documents, renders
the before/after bodies with PreviewPipelineBench --emit, and records a probe
(a heading after the edit, with its true 1-based source line) for each case.

    python tests/preview/shiftcases.py build/PreviewPipelineBench.exe build/preview/shiftdata.js
"""
import io
import json
import os
import subprocess
import sys
import tempfile

DOCS = ["samples/test-all-features.md", "samples/mermaid.md", "samples/standard-markdown.md"]


def changelog_doc():
    # Shaped like samples/large_file.md: release sections whose list items use
    # reference links, some defined in their own section and some only far
    # above (so a list below an edit holds numbers from both sides of it).
    out = ["Changelog", "=========", "", "[early]: https://example.com/early", ""]
    for r in range(12):
        out += ["Version 1.%d.0" % r, "=============", "", "Language", "--------"]
        for k in range(4):
            out.append("- [Item %d.%d uses a local link][l%d-%d] and the [early] one." % (r, k, r, k))
        out += ["", "Plain release prose for version %d, which is" % r,
                "long enough to be split by Enter.%s" % ("[^n%d]" % r if r % 4 == 0 else ""), ""]
        out += ["[l%d-%d]: https://example.com/%d/%d" % (r, k, r, k) for k in range(4)]
        out.append("")
    out += ["[^n%d]: Note for version %d." % (r, r) for r in range(0, 12, 4)]
    return "\n".join(out)


def find(lines, text, start=0):
    for i in range(start, len(lines)):
        if text in lines[i]:
            return i
    raise KeyError(text)


def is_prose(line):
    return bool(line) and (line[0].isalpha() or line[0] == "*")


def prose_pair(lines, start):
    # First paragraph line followed by another paragraph line of the same block.
    for i in range(start, len(lines) - 1):
        if is_prose(lines[i]) and is_prose(lines[i + 1]) and len(lines[i]) > 20:
            return i
    return None


def blank_between_paragraphs(lines, start):
    for i in range(max(1, start), len(lines) - 1):
        if lines[i] == "" and is_prose(lines[i - 1]) and is_prose(lines[i + 1]):
            return i
    raise KeyError("no adjacent paragraphs")


def outside_fence_headings(lines):
    out, fence = [], False
    for i, l in enumerate(lines):
        if l.lstrip().startswith("```"):
            fence = not fence
        elif not fence and (l.startswith("#") or (
                is_prose(l) and i + 1 < len(lines) and len(lines[i + 1]) > 2
                and set(lines[i + 1]) in ({"="}, {"-"}))):
            out.append(i)
    return out


def split_at(lines, i):
    l = lines[i]
    cut = l.find(" ", len(l) // 2)
    return lines[:i] + [l[:cut], l[cut + 1:]] + lines[i + 1:]


def generic_cases(lines):
    mid = len(lines) // 2
    body = lines.index("---", 1) + 1 if lines[0] == "---" else 0  # past front matter
    pair = prose_pair(lines, body)
    p = pair if pair is not None else next(
        i for i, l in enumerate(lines) if i >= body and is_prose(l) and len(l) > 20)
    c = {
        "same-line-char": lines[:p] + [lines[p] + " X"] + lines[p + 1:],
        "enter-split-paragraph": split_at(lines, p),
        "enter-blank-line-top": lines[:2] + [""] + lines[2:],
        "enter-blank-line-middle": lines[:mid] + [""] + lines[mid:],
        "enter-blank-line-end": lines[:-2] + [""] + lines[-2:],
        "insert-several-lines-middle": lines[:mid] + ["", "Inserted one.", "", "Inserted two.", ""] + lines[mid:],
        "delete-several-lines-top": lines[:3] + lines[7:],
    }
    if pair is not None:
        c["backspace-join-lines"] = lines[:p] + [lines[p] + " " + lines[p + 1]] + lines[p + 2:]
    return c


def all_features_cases(L):
    c = {}
    b = blank_between_paragraphs(L, 20)
    c["backspace-merge-paragraphs"] = L[:b] + L[b + 1:]
    i = find(L, "## Lists and task lists")
    c["insert-paragraph-before-heading"] = L[:i] + ["New prose line one", "and line two.", ""] + L[i:]
    i, j = find(L, "> [!TIP]"), find(L, "> [!IMPORTANT]")
    c["delete-callouts"] = L[:i] + L[j:]
    i = find(L, "  - katex")
    c["front-matter-add-tag"] = L[:i] + ["  - extra"] + L[i:]
    i = find(L, "# FastMD All Features Test")
    c["insert-before-first-heading"] = L[:i] + ["Intro line.", ""] + L[i:]
    i = find(L, "## Tables")
    c["heading-level-change"] = L[:i] + ["### Tables"] + L[i + 1:]
    i = find(L, "## Footnotes")
    c["heading-insert"] = L[:i] + ["## New Section", ""] + L[i:]
    i = find(L, "- Banana")
    c["list-add-item"] = L[:i + 1] + ["- Cherry"] + L[i + 1:]
    c["list-split-item"] = L[:i] + ["- Ban", "ana"] + L[i + 1:]
    i = find(L, "> A regular blockquote.")
    c["blockquote-add-line"] = L[:i + 1] + ["> Another quoted line."] + L[i + 1:]
    i = find(L, "| Mermaid | fenced block |")
    c["table-add-row"] = L[:i + 1] + ["| Extra | row | ✅ |"] + L[i + 1:]
    c["table-delete-row"] = L[:i] + L[i + 1:]
    i = find(L, "#include <iostream>")
    c["code-add-line"] = L[:i + 1] + ["#include <vector>"] + L[i + 1:]
    i = find(L, "```python")
    c["code-remove-opening-fence"] = L[:i] + L[i + 1:]
    j = find(L, "```", i + 1)
    c["code-remove-closing-fence"] = L[:j] + L[j + 1:]
    i = find(L, "## Lists and task lists")
    c["code-open-fence-mid-document"] = L[:i] + ["```"] + L[i:]
    i = find(L, "```mermaid")
    c["mermaid-add-line"] = L[:i + 2] + ["%% a comment"] + L[i + 2:]
    c["mermaid-insert-before"] = L[:i] + ["", ""] + L[i:]
    i = find(L, "$$")
    c["math-add-line"] = L[:i + 1] + ["a + b \\\\"] + L[i + 1:]
    p = prose_pair(L, 20)
    c["paragraph-to-heading"] = L[:p] + ["# " + L[p]] + L[p + 1:]
    c["setext-underline"] = L[:p + 1] + ["---"] + L[p + 1:]
    i = find(L, "[^one]:")
    c["footnote-blank-before"] = L[:i] + [""] + L[i:]
    return c


PAGE_BREAK = '<div style="break-after: page;"></div>'


def page_break_doc():
    blocks = ["# Heading", "- one\n  continuation\n- two",
              "1. one\n   continuation\n2. two", "> quote\n> continued",
              "```text\ncode\n```", "| A | B |\n|---|---|\n| a | b |",
              "***", "paragraph\ncontinued", PAGE_BREAK,
              "- [ ] task\n- [x] done", "## Source probe"]
    return ("\n" + PAGE_BREAK + "\n").join(blocks)


def page_break_cases(lines):
    first = lines.index(PAGE_BREAK)
    last = max(i for i, line in enumerate(lines) if line == PAGE_BREAK)
    quote = lines.index("> quote")
    item = lines.index("- two")
    return {
        "blank-after": lines[:first + 1] + [""] + lines[first + 1:],
        "blank-before": lines[:first] + [""] + lines[first:],
        "blank-both-sides": lines[:first] + ["", PAGE_BREAK, ""] + lines[first + 1:],
        "delete-first-break": lines[:first] + lines[first + 1:],
        "insert-consecutive-break": lines[:first] + [PAGE_BREAK] + lines[first:],
        "edit-after-break": lines[:first + 1] + ["- edited"] + lines[first + 2:],
        "edit-before-break": ["## Edited heading"] + lines[1:],
        "split-list": lines[:item] + [PAGE_BREAK] + lines[item:],
        "split-quote": lines[:quote + 1] + [PAGE_BREAK] + lines[quote + 1:],
        "break-at-eof": lines + [PAGE_BREAK],
        "remove-last-break": lines[:last] + lines[last + 1:],
    }


def soft_break_doc():
    return "\n".join([
        "# Soft breaks", "", "first", "second", "", "> quote one", "> quote two",
        "", "- item one", "  continued", "", "two spaces  ", "next", "",
        "backslash\\", "next", "", "Inline<br>", "HTML", "",
        "```", "code one", "code two", "```", "",
        "<div>", "raw one", "raw two", "</div>", "", "## Source probe"])


def soft_break_cases(lines):
    p = lines.index("first")
    q = lines.index("> quote one")
    return {
        "edit-soft-line": lines[:p] + ["edited first"] + lines[p + 1:],
        "soft-to-blank": lines[:p + 1] + [""] + lines[p + 1:],
        "soft-to-two-spaces": lines[:p] + ["first  "] + lines[p + 1:],
        "soft-to-backslash": lines[:p] + ["first\\"] + lines[p + 1:],
        "quote-to-blank": lines[:q + 1] + [">"] + lines[q + 1:],
        "CRLF": [line + "\r" for line in lines],
        "raw-edit": [line.replace("raw one", "raw edited") for line in lines],
    }


def main():
    bench, out = os.path.abspath(sys.argv[1]), sys.argv[2]
    tmp = tempfile.mkdtemp(prefix="fastmd-shift-")
    srcs, cases = {}, []
    docs = [(os.path.splitext(os.path.basename(d))[0],
             io.open(d, encoding="utf-8").read().replace("\r\n", "\n")) for d in DOCS]
    docs.append(("changelog", changelog_doc()))
    docs.append(("page-break", page_break_doc()))
    docs.append(("soft-break", soft_break_doc()))
    for stem, text in docs:
        lines = text.split("\n")
        if stem == "page-break":
            variants = page_break_cases(lines)
        elif stem == "soft-break":
            variants = soft_break_cases(lines)
        else:
            variants = generic_cases(lines)
        if stem == "test-all-features":
            variants.update(all_features_cases(lines))
        srcs[stem + "__base"] = lines
        for name, v in variants.items():
            srcs[stem + "__" + name] = v
            cases.append((stem, name))
    for key, lines in srcs.items():
        io.open(os.path.join(tmp, key + ".md"), "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    files = [os.path.join(tmp, k + ".md") for k in srcs]
    subprocess.run([bench, "--emit", tmp] + files, check=True, stdout=subprocess.DEVNULL)

    def body(key):
        return io.open(os.path.join(tmp, key + ".md.preview.html"), encoding="utf-8").read()

    data = []
    for stem, name in cases:
        lines = srcs[stem + "__" + name]
        h = outside_fence_headings(lines)[-1]
        data.append({
            "name": stem + " [" + name + "]",
            "base": body(stem + "__base"),
            "next": body(stem + "__" + name),
            "probe": {"text": lines[h].lstrip("# ").strip(), "line": h + 1},
        })
    io.open(out, "w", encoding="utf-8").write("window.__SHIFT_CASES = " + json.dumps(data) + ";\n")
    print("wrote %d cases to %s" % (len(data), out))


if __name__ == "__main__":
    main()
