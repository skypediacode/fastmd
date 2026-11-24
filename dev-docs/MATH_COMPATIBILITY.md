# Markdown math and copied expressions

FastMD protects math before MD4C parses Markdown. It then restores HTML-escaped
TeX, and the bundled KaTeX auto-renderer typesets it in the WebView2 preview.
PDF export, browser preview, HTML export and Markdown printing use the same
math preprocessor and preview shell. Small preview updates and large-document
patches therefore receive the same normalized math as a fresh render.

## Delimiter dialects

`$...$`, `$$...$$`, `\(...\)` and `\[...\]` contain literal TeX. For example,
`\_` means a printed underscore and `_` introduces a subscript. FastMD also
accepts the Markdown-escaped delimiters `\\(...\\)` and `\\[...\\]`.

Inside an expression using exactly two backslashes at its opening delimiter,
FastMD accepts `\_{...}` (including whitespace before the opening brace) as
the Markdown-escaped spelling of a braced subscript. This supports ChatGPT's
Markdown Copy output, including Vietnamese underbrace labels:

```latex
\\[ \underbrace{y-y_0}\_{\text{Độ thay đổi của }y} = \underbrace{f'(x_0)}\_{\text{Độ nghiêng}} \underbrace{(x-x_0)}\_{\text{Độ thay đổi của }x} \\]
```

Only the rendered snippet is normalized. Opening/saving the file, the editor,
undo history and clipboard source are unchanged. The rule is independent of
`\underbrace`: it also applies to ordinary braced subscripts and nested math.

## Literal content and compatibility trade-offs

- Standard delimiters and dollar math retain their existing TeX semantics.
- Text arguments (`\text`, its font variants, `\mbox`, `\operatorname` and
  `\url`) retain their escapes, including nested groups and escaped braces.
- Unbraced `\_i`, other TeX escapes, commands, matrix row separators `\\`,
  and extra literal backslash pairs preceding a delimiter are preserved.
- Code spans and fenced code remain protected. When MD4C classifies a
  placeholder as code, including indented code in lists, its original source
  spelling is restored instead of a normalized/rendered math snippet.
- Unmatched delimiters and ordinary prose are not normalized.

There is an unavoidable ambiguity in `\\[x\_{i}\\]`: it can mean copied
Markdown for a subscript, or literal TeX for an underscore followed by a group.
FastMD chooses the copied-subscript interpretation in this narrow dialect.
Use `\[x\_{i}\]` or dollar delimiters for the latter meaning. Unknown custom
macros with text arguments are not expanded by this lexical normalizer; use
standard delimiters when their literal escapes must be preserved.

## Comparison with other implementations

[Pandoc](https://pandoc.org/MANUAL.html#extension-tex_math_double_backslash)
documents single- and double-backslash delimiter extensions separately. Math
contents remain TeX; doubled delimiters alone do not universally imply decoding
Markdown escapes inside the expression.

The current [nav9v controller](https://nav9v.github.io/online-markdown-editor/script.js)
has several engine paths. Its Markdown-first paths run Markdown parsing before
KaTeX/MathJax; `\\[` becomes `\[` and `\_` becomes `_`. A standard `\[` can
instead lose its backslash before math detection. Its Marked/MathJax path has a
different order. The current [markdown.com.vn controller](https://markdown.com.vn/script.js)
also parses Markdown before MathJax. These behaviors explain the reported
results but are not a universal Markdown/TeX specification. Running ordinary
Markdown unescaping over all TeX would also damage legitimate escapes and
matrix row separators, which FastMD deliberately preserves.

## Regression checks

`MarkdownCompatibilityTest` checks both exact inputs, exports, text escapes,
subscripts, overbraces, code protection and source-line mappings.
`tests/preview/mathlayouttest.*` additionally measures real browser geometry,
compares standard/copied KaTeX DOMs, and exercises light/dark themes, edits,
C++ patches, standalone HTML and the print shell. See the preview test README
for fixture generation and execution.

Verification on 2026-10-10: `scripts/build.ps1` produced FastMD.exe; all five
browser harnesses (math layout, diff, decoration, shifts and patches) passed,
and the math harness also passed in Edge. Native tests passed 42/43. The
remaining `WorkspaceOutlinePersistenceTest` fails two search-button icon-cache
identity assertions (lines 261 and 270), including with the Windows platform;
these checks do not exercise math rendering.

CLI PDF export and browser print-to-PDF were visually inspected. Both equation
forms match, including Unicode labels and the other vertical constructs. The
shared print shell also produces a blank trailing page with this fixture and
with standard math alone; pagination was not changed by this compatibility fix.
Physical printer output was not exercised.
