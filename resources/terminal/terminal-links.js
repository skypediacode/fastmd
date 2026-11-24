/* FastMD terminal link detection.
 *
 * Pure text in, ranges out: no DOM, no xterm, no filesystem. terminal.js maps
 * the ranges onto buffer cells; the host re-parses and validates every link
 * before acting on it, so nothing here is a security decision -- the job is to
 * underline what a user would read as a link and to stay quiet on prose.
 *
 * detect(text) returns [{ start, end, text }] in code units, end exclusive,
 * sorted by start. `text` is what the host receives: the raw link, with a
 * quoted path re-quoted so the host sees one canonical form.
 */
(function (root) {
  'use strict';

  // A link longer than this is not something anyone hovers on purpose, and
  // bounding it keeps a minified-JS line from costing a long regex walk.
  var kMaxLinkLength = 2048;

  var URL_RE = /\b(?:https?|file):\/\/[^\s"'<>`\u0000-\u001f\u007f]+/gi;
  var QUOTED_RE = /(["'])([^"'\r\n]{1,1024}?)\1/g;
  // ':' is excluded so a line suffix ends the token; the drive alternative is
  // what admits the one legal colon. '=' and ',' split `--out=src/a.md` and
  // `a.md,b.md` into their paths.
  var TOKEN_RE = /[A-Za-z]:[\\/][^\s"'<>|*?`()\[\]{},;:=]*|[^\s"'<>|*?`()\[\]{},;:=]+/g;
  var SUFFIX_RE = /:(\d{1,7})(?::(\d{1,7}))?|\((\d{1,7})(?:, ?(\d{1,7}))?\)/y;
  var QUOTED_SUFFIX_RE = /:(\d{1,7})(?::(\d{1,7}))?|\((\d{1,7})(?:, ?(\d{1,7}))?\)|, line (\d{1,7})/y;
  var DRIVE_RE = /^[A-Za-z]:[\\/]/;
  var EXPLICIT_RELATIVE_RE = /^\.{1,2}[\\/]/;
  // An extension with at least one letter: `a.md`, `x.mp3`, not `1.2` or `4.5`.
  var EXTENSION_RE = /[^\\/.][^\\/]*\.(?=[A-Za-z0-9_+-]*[A-Za-z])[A-Za-z0-9_+-]{1,16}$/;
  // A trailing separator names a folder (`docs/`, `toan11\`); a letter keeps
  // `1/2/` out.
  var DIRECTORY_RE = /^[^\\/]+(?:[\\/][^\\/]+)*[\\/]$/;
  var LETTER_RE = /\p{L}/u;
  // A bare file name has no separator to say it is a path, so its extension
  // has to be one people open: documents, code, config, data, media. Endings
  // that are also common host names (com, net, org, io, app, dev) are left
  // out, which keeps `example.com:443` and `user@mail.com` plain.
  var BARE_EXTENSIONS = {};
  ('md markdown mdown mkd mkdn mdx txt text rst adoc asciidoc tex bib log csv tsv '
   + 'json jsonc json5 yaml yml toml ini cfg conf properties env xml xsd xsl plist '
   + 'html htm xhtml css scss sass less svg pdf epub docx doc xlsx xls pptx ppt odt ods odp rtf '
   + 'png jpg jpeg gif webp bmp ico tif tiff avif heic mp3 wav flac ogg mp4 mkv mov webm '
   + 'c h cc cpp cxx c++ hh hpp hxx inl ipp ino cs csx fs fsi fsx vb java kt kts scala groovy gradle '
   + 'go rs swift mm py pyi pyw ipynb rb php pl pm lua jl dart ex exs erl hs clj '
   + 'js mjs cjs jsx ts mts cts tsx vue svelte astro wasm '
   + 'sh bash zsh fish ps1 psm1 psd1 bat cmd sql graphql gql proto thrift '
   + 'cmake mk make mak ninja qrc ui pro pri qss diff patch lock sln vcxproj csproj props targets '
   + 'zip 7z rar tar gz tgz bz2 xz exe dll msi msix appx lnk iso').split(' ').forEach(function (ext) {
    BARE_EXTENSIONS[ext] = true;
  });
  // One space-separated word of a drive path's continuation: the token class.
  var WORD_RE = /[^\s"'<>|*?`()\[\]{},;:=]+/y;
  var NEW_LINK_RE = /^(?:[A-Za-z]:[\\/]|[A-Za-z][A-Za-z0-9+.-]*:\/\/)/;

  function count(text, ch) {
    var n = 0;
    for (var i = 0; i < text.length; i++) if (text.charAt(i) === ch) n++;
    return n;
  }

  var kClosers = { ')': '(', ']': '[', '}': '{' };

  // Sentence punctuation and an unmatched closing bracket belong to the prose
  // around a URL, not the URL: "(see https://x.y/a)." -> https://x.y/a, while
  // https://en.wikipedia.org/wiki/Foo_(bar) keeps its balanced parenthesis.
  function trimUrl(url) {
    for (;;) {
      var last = url.charAt(url.length - 1);
      if ('.,;:!?'.indexOf(last) >= 0) { url = url.slice(0, -1); continue; }
      var open = kClosers[last];
      if (open && count(url, last) > count(url, open)) { url = url.slice(0, -1); continue; }
      return url;
    }
  }

  function hasSeparator(path) {
    return path.indexOf('/') >= 0 || path.indexOf('\\') >= 0;
  }

  function isBareFileName(name) {
    if (!EXTENSION_RE.test(name)) return false;
    return BARE_EXTENSIONS[name.slice(name.lastIndexOf('.') + 1).toLowerCase()] === true;
  }

  // Shapes a path may take. A bare relative path needs both a separator and
  // an extension, which is what keeps `and/or`, `1/2`, `v1.2` and `e.g` from
  // being underlined. A lone `file.ext` qualifies with a known extension
  // (`README.md`, `notes.txt:12`) or, for any other extension, an explicit
  // position (`main.zig(42)`, `x.foo:3:5`). Existence is the host's call.
  function isPathShape(path, strongSuffix) {
    if (!path || path.length > kMaxLinkLength) return false;
    if (path.indexOf('://') >= 0) return false;
    if (DRIVE_RE.test(path)) return true;
    if (EXPLICIT_RELATIVE_RE.test(path)) return !/^\.\.?[\\/]$/.test(path);
    var first = path.charAt(0);
    // Root-relative and UNC paths are not linked: the first depends on the
    // shell's current drive, the second would open a network connection.
    if (first === '/' || first === '\\' || first === '~' || first === '-') return false;
    if (hasSeparator(path))
      return EXTENSION_RE.test(path) || (DIRECTORY_RE.test(path) && LETTER_RE.test(path));
    return isBareFileName(path) || (strongSuffix && EXTENSION_RE.test(path));
  }

  // Unquoted output cannot say where a path with spaces ends. A drive path
  // continues across single spaces up to the last word that reaches a deeper
  // segment (`C:\Program Files\App\a.exe`), completes a file name
  // (`C:\My Docs\bai tap.md`) or closes a prompt (`PS D:\My Folder>`). A
  // segment with an extension ends it, since a file has no children, and so
  // does a word that starts another path, a URL or a switch. The host falls
  // back to shorter prefixes when the longest does not exist. Returns the new
  // end, or `end` when nothing qualifies.
  function extendDrivePath(text, start, end) {
    var committed = end;
    var pos = end;
    while (pos - start < kMaxLinkLength && text.charAt(pos) === ' '
           && !EXTENSION_RE.test(text.slice(start, pos))) {
      var wordStart = pos + 1;
      var head = text.charAt(wordStart);
      if (head === '/' || head === '\\' || head === '-' || head === '~'
          || NEW_LINK_RE.test(text.slice(wordStart, wordStart + 64)))
        break;
      WORD_RE.lastIndex = wordStart;
      var word = WORD_RE.exec(text);
      if (!word) break;
      var trimmed = word[0].replace(/[.!]+$/, '');
      if (!trimmed) break;
      var wordEnd = wordStart + trimmed.length;
      var closesPrompt = text.charAt(wordEnd) === '>';
      if (hasSeparator(trimmed) || closesPrompt || EXTENSION_RE.test(text.slice(start, wordEnd)))
        committed = wordEnd;
      if (closesPrompt || trimmed.length !== word[0].length) break;
      pos = wordEnd;
    }
    return committed;
  }

  function overlaps(ranges, start, end) {
    for (var i = 0; i < ranges.length; i++)
      if (start < ranges[i].end && ranges[i].start < end) return true;
    return false;
  }

  function matchSuffix(re, text, at) {
    re.lastIndex = at;
    return re.exec(text);
  }

  // A one-number `:42` is weak (`example.com:443`, `12:30`); a column or the
  // MSVC parenthesised form is strong.
  function isStrongSuffix(match) {
    return !!match && (match[2] !== undefined || match[3] !== undefined);
  }

  function detect(text) {
    var links = [];
    if (!text) return links;
    var match;

    URL_RE.lastIndex = 0;
    while ((match = URL_RE.exec(text)) !== null) {
      var url = trimUrl(match[0]);
      if (url.length > kMaxLinkLength) continue;
      var schemeEnd = url.indexOf('://') + 3;
      // file:/// is a complete prefix; anything else needs a host.
      if (url.length <= schemeEnd + (url.charAt(schemeEnd) === '/' ? 1 : 0)) continue;
      links.push({ start: match.index, end: match.index + url.length, text: url });
    }

    QUOTED_RE.lastIndex = 0;
    while ((match = QUOTED_RE.exec(text)) !== null) {
      var quote = match[1];
      var content = match[2];
      var contentStart = match.index + 1;
      var quoteEnd = match.index + match[0].length;
      if (content !== content.trim()) { QUOTED_RE.lastIndex = match.index + 1; continue; }
      var quotedSuffix = matchSuffix(QUOTED_SUFFIX_RE, text, quoteEnd);
      var strong = isStrongSuffix(quotedSuffix) || (!!quotedSuffix && quotedSuffix[5] !== undefined);
      if (!isPathShape(content, strong)) { QUOTED_RE.lastIndex = match.index + 1; continue; }
      var end = quotedSuffix ? quoteEnd + quotedSuffix[0].length : contentStart + content.length;
      if (overlaps(links, match.index, quoteEnd)) continue;
      links.push({
        start: contentStart, end: end,
        text: quotedSuffix ? quote + content + quote + quotedSuffix[0] : content
      });
    }

    TOKEN_RE.lastIndex = 0;
    while ((match = TOKEN_RE.exec(text)) !== null) {
      var token = match[0];
      var start = match.index;
      var trimmed = token.replace(/[.!]+$/, '');
      var tokenEnd = start + trimmed.length;
      if (!trimmed || overlaps(links, start, start + token.length)) continue;
      if (trimmed.length === token.length && DRIVE_RE.test(trimmed)) {
        var extendedEnd = extendDrivePath(text, start, tokenEnd);
        if (!overlaps(links, start, extendedEnd)) {
          tokenEnd = extendedEnd;
          trimmed = text.slice(start, tokenEnd);
          token = trimmed;
        }
      }
      // A suffix has to touch the path itself: "a.md.:3" is prose.
      var suffix = trimmed.length === token.length ? matchSuffix(SUFFIX_RE, text, tokenEnd) : null;
      if (!isPathShape(trimmed, isStrongSuffix(suffix))) continue;
      // Glued to a `word:` it is that scheme's or key's value (`mailto:a@b.c`).
      if (start > 0 && text.charAt(start - 1) === ':' && !hasSeparator(trimmed)
          && !isStrongSuffix(suffix))
        continue;
      var linkEnd = suffix ? tokenEnd + suffix[0].length : tokenEnd;
      links.push({ start: start, end: linkEnd, text: text.slice(start, linkEnd) });
    }

    links.sort(function (a, b) { return a.start - b.start; });
    return links;
  }

  // --- shell-reported working directory ---------------------------------
  // Each returns the directory, or '' when the payload is not a cwd report.
  // The host validates whatever comes back; these only unwrap the encodings.

  // OSC 7: file://host/path, percent-encoded. MSYS shells report /c/Users/...
  function cwdFromOsc7(data) {
    var match = /^file:\/\/[^\/]*(\/.*)$/i.exec(data || '');
    if (!match) return '';
    var path;
    try { path = decodeURIComponent(match[1]); } catch (e) { return ''; }
    var msys = /^\/([A-Za-z])(\/.*)?$/.exec(path);
    if (msys) return msys[1].toUpperCase() + ':' + (msys[2] || '/');
    if (/^\/[A-Za-z]:/.test(path)) return path.slice(1);
    return '';
  }

  // OSC 9;9 (Windows Terminal / ConEmu): 9;"C:\path" or 9;C:\path. Other
  // OSC 9 payloads are notifications and are ignored.
  function cwdFromOsc9(data) {
    var match = /^9;(.*)$/.exec(data || '');
    if (!match) return '';
    var path = match[1];
    if (path.length >= 2 && path.charAt(0) === '"' && path.charAt(path.length - 1) === '"')
      path = path.slice(1, -1);
    return path;
  }

  // OSC 633;P;Cwd=... (VS Code shell integration), with \\ and \xHH escapes.
  function cwdFromOsc633(data) {
    var match = /^P;Cwd=(.*)$/.exec(data || '');
    if (!match) return '';
    return match[1].replace(/\\(\\|x([0-9A-Fa-f]{2}))/g, function (all, escaped, hex) {
      return hex ? String.fromCharCode(parseInt(hex, 16)) : '\\';
    });
  }

  root.FastMDTerminalLinks = {
    detect: detect,
    cwdFromOsc7: cwdFromOsc7,
    cwdFromOsc9: cwdFromOsc9,
    cwdFromOsc633: cwdFromOsc633
  };
})(window);
