# MD4C patch

`md4c-table-interrupts-paragraph.patch` applies to `src/md4c.c` of **MD4C 0.5.2**
(tag `release-0.5.2`, pinned in the root `CMakeLists.txt`). `cmake/PatchMd4c.cmake`
applies it at configure time.

## Why

MD4C starts a table only if the header row is the paragraph's sole line, so

```markdown
- item:
  | a | b |
  |---|---|
  | 1 | 2 |
```

renders as text. GFM lets a table interrupt a paragraph. Do not emulate this by
inserting blank lines before parsing: a blank line between an item's blocks makes
the whole list loose, and a line scanner cannot reliably know the container, code
and HTML context.

## What it changes

Only a delimiter row after a paragraph of **two or more lines** is affected. If it
is indented less than a code block and the paragraph's last line has the same
number of cells, that line is split off as the table header. Link reference
definitions in the remaining lines are still consumed. A one-line paragraph takes
the upstream path unchanged.

Header cells are counted with only a backslash protecting `|`. The count is
therefore never lower than what MD4C finds when it renders the row, so no header
cell can be dropped.

## Known differences from cmark-gfm

Compared with cmark-gfm `7fe8904`. These are accepted, not to-do items.

Interrupting tables:

- After one mismatched delimiter row cmark-gfm gives up on that paragraph; FastMD
  still accepts a later matching pair.
- cmark-gfm counts the leading spaces of a lazy header row as a cell; FastMD does
  not.
- A link reference definition above the header stays a definition; cmark-gfm
  turns it into text.
- `\|` is an escaped backslash followed by a boundary; cmark-gfm reads an escaped
  pipe.

Upstream MD4C behavior, not changed here:

- A bare `|` inside a code span does not split a rendered cell, and `\|` inside
  one keeps its backslash.
- A one-line paragraph becomes a table even if the column counts differ.

## How it is applied

The fetched checkout is never modified. A copy in `<build>/md4c-patched/work` is
refreshed from it and patched on every configure, then copied to
`<build>/md4c-patched/md4c.c` only if it changed. A patch that does not apply, or
changes nothing, fails the configure step. Git is required, as for FetchContent.
The patch must keep LF line endings (`.gitattributes`).

## Upgrading MD4C

1. Check whether the release lets a table interrupt a paragraph. If so, delete the
   patch and the `fastmd_patch_md4c()` call, keeping the tests.
2. Otherwise change `GIT_TAG` and configure. If the patch fails, port it by hand
   onto the new `md4c.c` and regenerate it as a unified diff with `a/md4c.c` and
   `b/md4c.c` labels. Do not loosen the matching to force it.
3. Even if it applies, review the code it relies on: the table check in
   `md_analyze_line()`, the `MD_LINE_TABLEUNDERLINE` branch of `md_process_line()`,
   `md_end_current_block()`, the block-bytes layout, and the `MD_LINE_ANALYSIS`
   field order.
4. Build, then run `MarkdownCompatibilityTest` (tables in containers, tight/loose
   lists, source lines), `MarkdownExtensionsTest`, `MarkdownSoftBreakTest` and
   `PreviewPatchTest`.
5. Update the version above and in the root `README.md`.
