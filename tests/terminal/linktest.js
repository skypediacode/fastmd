// Link detection and the hover/Ctrl+Click policy, driven through the real
// terminal.js and xterm.js with synthetic mouse and key events.
(function () {
  var results = [];
  var failures = 0;
  var D = window.FastMDTerminalLinks;

  function check(condition, label, detail) {
    if (!condition) failures++;
    results.push((condition ? 'PASS ' : 'FAIL ') + label + (condition || !detail ? '' : '  -- ' + detail));
  }

  function show(value) { return JSON.stringify(value); }

  // --- 1. pure detection --------------------------------------------------

  function expectLinks(line, expected, label) {
    var got = D.detect(line).map(function (link) { return link.text; });
    check(show(got) === show(expected), label || line, 'expected ' + show(expected) + ', got ' + show(got));
  }

  function expectRange(line, expectedUnderlined, label) {
    var got = D.detect(line).map(function (link) { return line.slice(link.start, link.end); });
    check(show(got) === show(expectedUnderlined), label, 'expected ' + show(expectedUnderlined) + ', got ' + show(got));
  }

  function detection() {
    expectLinks('https://github.com', ['https://github.com']);
    expectLinks('open http://localhost:3000 now', ['http://localhost:3000']);
    expectLinks('https://example.com/path?q=hello', ['https://example.com/path?q=hello']);
    expectLinks('https://example.com/file.md#section', ['https://example.com/file.md#section']);
    expectLinks('see https://example.com/a.', ['https://example.com/a'], 'URL: trailing period dropped');
    expectLinks('(see https://example.com/a).', ['https://example.com/a'], 'URL: unbalanced paren dropped');
    expectLinks('https://en.wikipedia.org/wiki/Foo_(bar)', ['https://en.wikipedia.org/wiki/Foo_(bar)'],
      'URL: balanced paren kept');
    expectLinks('[x](https://example.com/y)', ['https://example.com/y'], 'URL: Markdown link');
    expectLinks('"https://example.com/q" \'https://example.com/r\'',
      ['https://example.com/q', 'https://example.com/r'], 'URL: quoted');
    expectLinks('<https://example.com/z>', ['https://example.com/z'], 'URL: angle brackets');
    expectLinks('https://a.com, https://b.com; https://c.com!',
      ['https://a.com', 'https://b.com', 'https://c.com'], 'URL: several on a line');
    expectLinks('https:// and http://', [], 'URL: scheme alone is not a link');
    expectLinks('javascript:alert(1) ftp://x.y mailto:a@b.c', [], 'URL: other schemes not detected');

    expectLinks('E:\\Projects\\fastmd-src\\README.md', ['E:\\Projects\\fastmd-src\\README.md']);
    expectLinks('C:/Projects/FastMD/docs/guide.md', ['C:/Projects/FastMD/docs/guide.md']);
    expectLinks('"C:\\My Projects\\notes.md"', ['C:\\My Projects\\notes.md'], 'path: quoted with spaces');
    expectRange('"C:\\My Projects\\notes.md"', ['C:\\My Projects\\notes.md'], 'path: quotes not underlined');
    expectLinks('E:\\Projects\\fastmd-src', ['E:\\Projects\\fastmd-src'], 'directory: absolute');
    expectLinks('C:\\', ['C:\\'], 'directory: drive root');
    expectLinks('file:///E:/Projects/fastmd-src/README.md', ['file:///E:/Projects/fastmd-src/README.md']);
    expectLinks('src/main.cpp ./README.md ../docs/guide.md resources/preview/preview.js',
      ['src/main.cpp', './README.md', '../docs/guide.md', 'resources/preview/preview.js'], 'relative paths');
    expectLinks('./docs and ..\\up', ['./docs', '..\\up'], 'directory: explicit relative');
    expectLinks('docs/\u6587\u6863/\u00e9t\u00e9.md', ['docs/\u6587\u6863/\u00e9t\u00e9.md'], 'Unicode path');
    expectLinks('src/main.cpp:42', ['src/main.cpp:42']);
    expectLinks('src/main.cpp:42:15: error', ['src/main.cpp:42:15'], 'gcc: trailing colon dropped');
    expectLinks('C:\\Projects\\FastMD\\src\\main.cpp:120:8', ['C:\\Projects\\FastMD\\src\\main.cpp:120:8'],
      'drive colon is not a line separator');
    expectLinks('src/main.cpp(42)', ['src/main.cpp(42)']);
    expectLinks('src/main.cpp(42,15): error C2065', ['src/main.cpp(42,15)'], 'MSVC form');
    expectLinks('main.cpp(42, 15)', ['main.cpp(42, 15)'], 'bare file with MSVC position');
    expectLinks('main.c:3:5', ['main.c:3:5'], 'bare file with line and column');
    expectLinks('File "x.py", line 42, in <module>', ['"x.py", line 42'], 'Python traceback');
    expectRange('File "x.py", line 42', ['x.py", line 42'], 'Python traceback: range');
    expectLinks('"C:\\My Projects\\a.md":3:4', ['"C:\\My Projects\\a.md":3:4'], 'quoted path with position');
    expectLinks('see ./README.md.', ['./README.md'], 'path: trailing period dropped');
    expectLinks('(src/a.md)', ['src/a.md'], 'path: parentheses dropped');
    expectLinks('--out=src/a.md a.md,b/c.md', ['src/a.md', 'a.md', 'b/c.md'], 'path: = and , split');
    expectLinks('https://x.com/src/a.md', ['https://x.com/src/a.md'], 'path inside URL is not separate');

    // Shapes from the manual test that did not link before.
    expectLinks('toan11/', ['toan11/'], 'directory: bare relative with trailing separator');
    expectLinks('ls toan11\\ docs/sub/', ['toan11\\', 'docs/sub/'], 'directory: bare relative, both separators');
    expectLinks('1/2/ 3\\', [], 'directory: digits only are not linked');
    expectLinks('D:/Lucy Cua Nam Wife Chip/MinhMaiAnh/', ['D:/Lucy Cua Nam Wife Chip/MinhMaiAnh/'],
      'drive path with spaces');
    expectLinks('C:\\Program Files\\Example App\\example.exe',
      ['C:\\Program Files\\Example App\\example.exe'], 'drive file with spaces');
    expectLinks('C:\\My Docs\\bai tap.md:12:4: error', ['C:\\My Docs\\bai tap.md:12:4'],
      'drive file with spaces in its name and a position');
    expectLinks('PS D:\\Lucy Cua Nam Wife Chip\\MinhMaiAnh> echo toan11/',
      ['D:\\Lucy Cua Nam Wife Chip\\MinhMaiAnh', 'toan11/'], 'PowerShell prompt with spaces');
    expectLinks('PS D:\\My Folder> git status', ['D:\\My Folder'], 'prompt ends the path');
    expectLinks('    Directory: D:\\Lucy Cua\\MinhMaiAnh', ['D:\\Lucy Cua\\MinhMaiAnh'],
      'Get-ChildItem header');
    expectLinks('wrote C:\\out\\x and then stopped', ['C:\\out\\x'], 'prose after a drive path stays plain');
    expectLinks('C:\\a.md and C:\\b.md', ['C:\\a.md', 'C:\\b.md'], 'a file ends a path: two links');
    expectLinks('dir C:\\x /s', ['C:\\x'], 'a switch ends a path');
    expectLinks('C:\\x  two\\y.md', ['C:\\x', 'two\\y.md'], 'two spaces end a path');
    expectLinks('C:\\x and https://e.com/a', ['C:\\x', 'https://e.com/a'], 'a URL ends a path');
    expectRange('see "C:\\My Docs\\a.md" or C:\\My Docs\\b.md.',
      ['C:\\My Docs\\a.md', 'C:\\My Docs\\b.md'], 'quoted and unquoted spaces');
    expectLinks('file:///C:/My%20Docs/a.md:12:4', ['file:///C:/My%20Docs/a.md:12:4'],
      'file URL with position');

    expectLinks('e.g. v1.2 foo:bar 12:30 and/or 1/2.5 plain words here', [], 'prose is not linked');
    expectLinks('example.com:443 main.cpp:42', ['main.cpp:42'],
      'host:port stays plain; a known extension links with a single number');

    // Bare names and backslash-relative paths: underlined with no position,
    // since nothing but the extension says they are files.
    expectLinks('README.md', ['README.md'], 'bare: README.md');
    expectLinks('README.md:12', ['README.md:12'], 'bare: README.md:12');
    expectLinks('toan11\\on-tap.md', ['toan11\\on-tap.md'], 'relative: backslash');
    expectLinks('toan11\\on-tap.md:24:5', ['toan11\\on-tap.md:24:5'], 'relative: backslash with position');
    expectLinks('./toan11/on-tap.md', ['./toan11/on-tap.md'], 'relative: explicit');
    expectLinks('-a---   10/28/2026  5:00 PM   1234 ung-dung.md',
      ['ung-dung.md'], 'bare: Get-ChildItem row');
    expectLinks('\tmodified:   README.md', ['README.md'], 'bare: git status row');
    expectLinks("see 'notes.txt' and NOTES.MD.", ['notes.txt', 'NOTES.MD'], 'bare: quoted, upper case, sentence');
    expectLinks('user@mail.com github.com www.example.org app.dev', [], 'bare: host names stay plain');
    expectLinks('x.unknownext x.zz', [], 'bare: unknown extension needs a position');
    expectLinks('/usr/bin/env \\Windows\\x.md \\\\server\\share\\a.md ~/x.md', [],
      'root-relative, UNC and ~ are not linked');
    expectLinks("it's 'a test' don't", [], 'apostrophes are not quoted paths');
    expectLinks('a.md.:3', ['a.md'], 'suffix must touch the path');
    expectLinks('mailto:x.md key:value.txt', [], 'bare name glued to scheme: or key: stays plain');

    check(D.cwdFromOsc7('file://host/C:/Users/me') === 'C:/Users/me', 'OSC 7 drive path');
    check(D.cwdFromOsc7('file://host/c/Users/me%20x') === 'C:/Users/me x', 'OSC 7 MSYS path, decoded');
    check(D.cwdFromOsc7('file://host/home/me') === '', 'OSC 7 Unix path ignored');
    check(D.cwdFromOsc9('9;"C:\\Users\\me"') === 'C:\\Users\\me', 'OSC 9;9 quoted');
    check(D.cwdFromOsc9('4;1;50') === '', 'OSC 9 progress ignored');
    check(D.cwdFromOsc633('P;Cwd=C:\\\\x\\x3by') === 'C:\\x;y', 'OSC 633 escapes');
    check(D.cwdFromOsc633('A') === '', 'OSC 633 prompt mark ignored');
  }

  // --- 2. hover and activation through xterm ------------------------------

  function output(id, text) {
    var bytes = new TextEncoder().encode(text);
    var binary = '';
    for (var i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    window.keytestSendToPage({ type: 'output', id: id, data: btoa(binary) });
  }

  function wait(ms) { return new Promise(function (resolve) { setTimeout(resolve, ms); }); }

  // xterm parses and renders asynchronously (timers, then animation frames).
  function waitFor(condition, ms) {
    var waited = 0;
    return new Promise(function (resolve) {
      (function poll() {
        if (condition() || waited >= ms) {
          resolve(condition());
          return;
        }
        waited += 16;
        setTimeout(poll, 16);
      })();
    });
  }


  function hostEl(id) { return document.querySelector('.terminal-host[data-session="' + id + '"]'); }
  function rows(id) { return hostEl(id).querySelectorAll('.xterm-rows > div'); }
  function rowText(id, row) { return rows(id)[row] ? rows(id)[row].textContent : ''; }
  function screen(id) { return hostEl(id).querySelector('.xterm-screen'); }

  function underlined(id) {
    var text = '';
    hostEl(id).querySelectorAll('.xterm-rows span').forEach(function (span) {
      if (span.style.textDecoration.indexOf('underline') >= 0) text += span.textContent;
    });
    return text.replace(/ /g, ' ');
  }

  // Screen point at the middle of character `index` of a row's text.
  function pointAt(id, row, index) {
    var walker = document.createTreeWalker(rows(id)[row], NodeFilter.SHOW_TEXT);
    var node;
    var seen = 0;
    while ((node = walker.nextNode())) {
      if (index < seen + node.length) {
        var range = document.createRange();
        range.setStart(node, index - seen);
        range.setEnd(node, index - seen + 1);
        var rect = range.getBoundingClientRect();
        return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
      }
      seen += node.length;
    }
    return null;
  }

  function mouse(type, point, ctrl) {
    var target = document.elementFromPoint(point.x, point.y) || document.body;
    var event = new MouseEvent(type, {
      bubbles: true, cancelable: true, view: window, clientX: point.x, clientY: point.y,
      button: 0, buttons: type === 'mousedown' || type === 'mousemove' && mouse.down ? 1 : 0,
      detail: type === 'mousemove' ? 0 : 1, ctrlKey: !!ctrl
    });
    target.dispatchEvent(event);
    return event;
  }

  function key(type, ctrl) {
    var event = new KeyboardEvent(type, {
      bubbles: true, cancelable: true, key: 'Control', code: 'ControlLeft', ctrlKey: ctrl
    });
    (document.activeElement || document.body).dispatchEvent(event);
  }

  function posted(type) {
    return window.keytestPosted.filter(function (message) { return message.type === type; });
  }

  // `reachedXterm`: whether the press got past the screen to the .xterm root,
  // where xterm's selection and mouse reporting listen. (xterm calls
  // preventDefault on every press itself, so that flag says nothing.)
  function click(point, ctrl, id) {
    window.keytestPosted.length = 0;
    var root = hostEl(id || 1).querySelector('.xterm');
    var reached = false;
    function note() { reached = true; }
    root.addEventListener('mousedown', note);
    mouse('mousedown', point, ctrl);
    root.removeEventListener('mousedown', note);
    mouse('mouseup', point, ctrl);
    return { reachedXterm: reached, links: posted('openLink'), inputs: posted('input') };
  }

  function cursorAt(id) { return window.getComputedStyle(screen(id)).cursor; }

  function colorAt(point) {
    var element = document.elementFromPoint(point.x, point.y);
    return element ? window.getComputedStyle(element).color : '';
  }

  // Hover, Ctrl, plain click, Ctrl+Click and leave for one link.
  async function exerciseLink(id, label, point, away, expectUnderline, expectText) {
    var before = colorAt(point);
    mouse('mousemove', point, false);
    check(colorAt(point) === before, label + ': hover keeps the text color', before + ' -> ' + colorAt(point));
    check(underlined(id) === expectUnderline, label + ': plain hover underlines exactly the link',
      'underlined ' + show(underlined(id)));
    check(!document.documentElement.classList.contains('link-modifier') && cursorAt(id) !== 'pointer',
      label + ': no hand cursor without Ctrl', cursorAt(id));

    key('keydown', true);
    check(cursorAt(id) === 'pointer', label + ': Ctrl while hovering shows the hand', cursorAt(id));
    check(underlined(id) === expectUnderline, label + ': underline survives pressing Ctrl');
    key('keyup', false);
    check(cursorAt(id) !== 'pointer', label + ': releasing Ctrl restores the cursor', cursorAt(id));
    check(underlined(id) === expectUnderline, label + ': underline survives releasing Ctrl');

    var plain = click(point, false);
    check(plain.links.length === 0, label + ': plain click does not open', show(plain.links));

    mouse('mousemove', point, true);
    var ctrl = click(point, true);
    check(ctrl.links.length === 1 && ctrl.links[0].text === expectText && ctrl.links[0].id === id,
      label + ': Ctrl+Click opens exactly once', show(ctrl.links));
    check(!ctrl.reachedXterm, label + ': Ctrl+Click press is kept from selection and mouse reporting');
    check(plain.reachedXterm, label + ': plain press still reaches xterm');

    mouse('mousemove', away, false);
    check(underlined(id) === '', label + ': leaving removes the underline', show(underlined(id)));
  }

  async function integration() {
    window.keytestSendToPage({ type: 'create', id: 1 });
    window.keytestSendToPage({ type: 'show', id: 1 });
    var longUrl = 'https://example.com/' + new Array(141).join('x');
    output(1, 'see https://example.com/a?b=1#c, ok\r\n'
      + 'src/main.cpp:42:15: error\r\n'
      + '\x1b]8;;https://example.com/osc\x1b\\OSC link\x1b]8;;\x1b\\ trailing\r\n'
      + 'plain prose only\r\n'
      + '\u6587\u6863 docs/\u8bf4\u660e.md\r\n'
      + longUrl + '\r\n'
      + 'end\r\n');
    var rendered = await waitFor(function () { return rowText(1, 7).indexOf('end') === 0; }, 5000);
    check(rendered, 'terminal rendered the test output', show(rowText(1, 0)));
    var away = pointAt(1, 3, 2);

    await exerciseLink(1, 'web URL', pointAt(1, 0, 8), away,
      'https://example.com/a?b=1#c', 'https://example.com/a?b=1#c');
    await exerciseLink(1, 'file:line:col', pointAt(1, 1, 3), away,
      'src/main.cpp:42:15', 'src/main.cpp:42:15');
    await exerciseLink(1, 'OSC 8', pointAt(1, 2, 2), away, 'OSC link', 'https://example.com/osc');
    await exerciseLink(1, 'wide-glyph row', pointAt(1, 4, 6), away,
      'docs/\u8bf4\u660e.md', 'docs/\u8bf4\u660e.md');

    window.keytestSendToPage({ type: 'theme', dark: true, fs: 15 });
    await wait(100);
    await exerciseLink(1, 'dark theme', pointAt(1, 0, 8), away,
      'https://example.com/a?b=1#c', 'https://example.com/a?b=1#c');
    window.keytestSendToPage({ type: 'theme', dark: false, fs: 15 });
    await wait(100);

    // A URL wrapped onto a second row is one link, hovered from either row.
    var wrapped = rowText(1, 6).indexOf('xxx') === 0;
    mouse('mousemove', pointAt(1, 6, 3), false);
    check(wrapped && underlined(1) === longUrl, 'wrapped URL: underline spans both rows',
      'underlined ' + underlined(1).length + ' chars');
    var wrappedClick = click(pointAt(1, 6, 3), true);
    check(wrappedClick.links.length === 1 && wrappedClick.links[0].text === longUrl,
      'wrapped URL: Ctrl+Click on the second row opens the whole URL');
    mouse('mousemove', away, false);

    mouse('mousemove', pointAt(1, 0, 8), false);
    await wait(650);
    var tooltip = document.querySelector('.term-link-tooltip');
    check(!!tooltip && tooltip.style.display === 'block'
      && tooltip.textContent === 'Follow link (Ctrl + Click)', 'tooltip appears on hover');
    mouse('mousemove', away, false);
    check(tooltip && tooltip.style.display === 'none', 'tooltip hides on leave');

    mouse('mousemove', pointAt(1, 0, 8), false);
    window.dispatchEvent(new Event('blur'));
    check(underlined(1) === '', 'window blur clears the underline', show(underlined(1)));
    mouse('mousemove', away, false);

    mouse('mousemove', pointAt(1, 3, 1), false);
    check(underlined(1) === '', 'prose is never underlined');
    var prose = click(pointAt(1, 3, 1), true);
    check(prose.links.length === 0 && prose.reachedXterm,
      'Ctrl+Click off a link is left to xterm');

    // Dragging across a link still selects text instead of opening it.
    window.keytestPosted.length = 0;
    mouse.down = true;
    mouse('mousedown', pointAt(1, 0, 0), false);
    mouse('mousemove', pointAt(1, 0, 20), false);
    mouse('mouseup', pointAt(1, 0, 20), false);
    mouse.down = false;
    await wait(100);
    check(posted('openLink').length === 0, 'drag-select over a link does not open it');
    check(hostEl(1).querySelectorAll('.xterm-selection div').length > 0, 'drag-select still selects');

    // A TUI with mouse tracking: plain clicks are its input; Ctrl+Click on a
    // link opens it and is not reported to the application.
    // SGR encoding (?1006), as real TUIs request: xterm sends the default X10
    // encoding through onBinary, which this page does not forward.
    output(1, '\x1b[?1000h\x1b[?1006h');
    await wait(100);
    mouse('mousemove', pointAt(1, 0, 8), false);
    var tuiPlain = click(pointAt(1, 0, 8), false);
    check(tuiPlain.links.length === 0 && tuiPlain.inputs.length > 0,
      'mouse mode: plain click goes to the application', show(tuiPlain));
    var tuiCtrl = click(pointAt(1, 0, 8), true);
    check(tuiCtrl.links.length === 1 && tuiCtrl.inputs.length === 0,
      'mouse mode: Ctrl+Click opens and reports nothing', show(tuiCtrl));
    output(1, '\x1b[?1006l\x1b[?1000l');
    mouse('mousemove', away, false);

    // Shell-reported cwd travels with the link.
    output(1, '\x1b]9;9;"C:\\Reported\\Dir"\x07');
    await wait(100);
    mouse('mousemove', pointAt(1, 1, 3), false);
    var withCwd = click(pointAt(1, 1, 3), true);
    check(withCwd.links.length === 1 && withCwd.links[0].cwd === 'C:\\Reported\\Dir',
      'OSC 9;9 cwd is sent with the link', show(withCwd.links));
    mouse('mousemove', away, false);

    await shellShapes();
  }

  // What the shells FastMD starts actually print: PowerShell's injected prompt
  // (quoted OSC 9;9, ST) and cmd's (unquoted, as ConPTY may rewrite it, BEL),
  // then the links from the manual test. Every Ctrl+Click must carry the whole
  // link and the directory the shell reported last.
  async function shellShapes() {
    window.keytestSendToPage({ type: 'create', id: 2 });
    window.keytestSendToPage({ type: 'show', id: 2 });
    var cwd = 'D:\\Lucy Cua Nam Wife Chip\\MinhMaiAnh';
    output(2, '\x1b]9;9;"' + cwd + '"\x1b\\PS ' + cwd + '> ls\r\n'
      + 'toan11/daoham.md:12:4\r\n'
      + 'toan11/\r\n'
      + 'D:/Lucy Cua Nam Wife Chip/MinhMaiAnh/\r\n'
      + 'C:\\Program Files\\Example App\\example.exe\r\n'
      + 'file:///D:/Lucy%20Cua/a.md\r\n'
      + '-a---  10/28/2026  5:00 PM  1234 ung-dung.md\r\n'
      + 'README.md:12\r\n'
      + 'toan11\\on-tap.md:24:5\r\n'
      + 'D:\\Lucy Cua Nam Wife Chip\\MinhMaiAnh\\toan11\\on-tap.md\r\n'
      + 'done\r\n');
    var rendered = await waitFor(function () { return rowText(2, 10).indexOf('done') === 0; }, 5000);
    check(rendered, 'shell shapes rendered', show(rowText(2, 0)));
    var away = pointAt(2, 10, 1);
    var cases = [
      [0, 12, cwd],
      [1, 3, 'toan11/daoham.md:12:4'],
      [2, 2, 'toan11/'],
      [3, 12, 'D:/Lucy Cua Nam Wife Chip/MinhMaiAnh/'],
      [4, 20, 'C:\\Program Files\\Example App\\example.exe'],
      [5, 12, 'file:///D:/Lucy%20Cua/a.md'],
      [6, 36, 'ung-dung.md'],
      [7, 2, 'README.md:12'],
      [8, 8, 'toan11\\on-tap.md:24:5'],
      [9, 30, 'D:\\Lucy Cua Nam Wife Chip\\MinhMaiAnh\\toan11\\on-tap.md']
    ];
    cases.forEach(function (c) {
      mouse('mousemove', pointAt(2, c[0], c[1]), false);
      check(underlined(2) === c[2], 'shell shape underlined: ' + c[2], show(underlined(2)));
      var opened = click(pointAt(2, c[0], c[1]), true, 2);
      check(opened.links.length === 1 && opened.links[0].text === c[2] && opened.links[0].cwd === cwd,
        'shell shape Ctrl+Click: ' + c[2], show(opened.links));
      mouse('mousemove', away, false);
    });

    output(2, '\x1b]9;9;C:\\Users\\Ng\u01b0\u1eddi D\u00f9ng\x07C:\\Users\\Ng\u01b0\u1eddi D\u00f9ng>\r\n');
    await wait(100);
    mouse('mousemove', pointAt(2, 1, 3), false);
    var cmdCwd = click(pointAt(2, 1, 3), true, 2);
    check(cmdCwd.links.length === 1 && cmdCwd.links[0].cwd === 'C:\\Users\\Ng\u01b0\u1eddi D\u00f9ng',
      'cmd-style OSC 9;9 cwd (unquoted, BEL, non-ASCII) is sent', show(cmdCwd.links));
    mouse('mousemove', away, false);
  }

  (async function () {
    try {
      detection();
      await integration();
    } catch (error) {
      failures++;
      results.push('FAIL harness threw: ' + (error && error.stack || error));
    }
    results.push(failures === 0 ? 'LINKTEST_VERDICT ALL PASSED'
                                : 'LINKTEST_VERDICT ' + failures + ' FAILED');
    document.getElementById('result').textContent =
      'LINKTEST_BEGIN\n' + results.join('\n') + '\nLINKTEST_END';
  })();
})();
