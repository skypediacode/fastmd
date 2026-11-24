/* FastMD terminal shell.
 *
 * One page, N xterm.js instances -- one per ConPTY session in the host. The
 * host addresses them by numeric session id.
 *
 * Host -> page:  create | destroy | show | output | theme | clear
 *                copy | paste | selectAll
 * Page -> host:  ready | input | resize | copy | requestPaste | openLink
 *
 * Bytes, not strings: ConPTY output is UTF-8 and a multi-byte sequence can be
 * split across two reads on the host side, so output arrives base64-encoded and
 * each session decodes it through its own *streaming* TextDecoder. Decoding
 * per-chunk without {stream:true} would corrupt any character unlucky enough to
 * land on a chunk boundary -- the reason CJK and emoji output is a real test
 * case rather than a formality.
 */
(function () {
  'use strict';

  var host = (window.chrome && window.chrome.webview) ? window.chrome.webview : null;
  var container = document.getElementById('terminals');
  var sessions = Object.create(null);
  var activeId = -1;
  var fontSize = 15;
  var dark = false;
  // A white terminal needs a stronger target than the UI's normal text: thin
  // monospace glyphs spend much of each cell in anti-aliasing, so 4.5:1 still
  // reads as grey. WCAG AAA keeps prose as crisp as the native Windows console
  // stack on the same display.
  var minimumContrastRatio = 6;

  function post(message) { if (host) host.postMessage(message); }

  // --- links --------------------------------------------------------------
  // Hover underlines a link whatever the modifiers; only Ctrl+Click follows
  // it, so only Ctrl turns the cursor into a hand. xterm's Linkifier owns the
  // hover lifecycle (it clears on leave, resize, scroll and on output under
  // the link); this page supplies the links, the tooltip and the Ctrl gate.
  // terminal.css keys the pointer cursor off the class set here.
  var linkModifier = false;
  var linkTooltip = null;
  var linkTooltipTimer = 0;
  // Bounds how far a hover looks along a wrapped line, so a megabyte of
  // unbroken output never turns one mouse move into a long scan.
  var kLinkRowsAround = 8;
  var kLinkTooltipDelayMs = 500;

  function isLinkModifier(event) {
    return !!event && event.ctrlKey && !event.altKey && !event.metaKey;
  }

  function setLinkModifier(on) {
    if (on === linkModifier) return;
    linkModifier = on;
    document.documentElement.classList.toggle('link-modifier', on);
  }

  function showLinkTooltip(event) {
    hideLinkTooltip();
    var x = event.clientX;
    var y = event.clientY;
    linkTooltipTimer = window.setTimeout(function () {
      linkTooltipTimer = 0;
      if (!linkTooltip) {
        linkTooltip = document.createElement('div');
        linkTooltip.className = 'term-link-tooltip';
        linkTooltip.textContent = 'Follow link (Ctrl + Click)';
        document.body.appendChild(linkTooltip);
      }
      linkTooltip.style.display = 'block';
      var left = Math.min(x, window.innerWidth - linkTooltip.offsetWidth - 4);
      var top = y + 18;
      if (top + linkTooltip.offsetHeight > window.innerHeight - 4)
        top = y - linkTooltip.offsetHeight - 8;
      linkTooltip.style.left = Math.max(4, left) + 'px';
      linkTooltip.style.top = Math.max(4, top) + 'px';
    }, kLinkTooltipDelayMs);
  }

  function hideLinkTooltip() {
    if (linkTooltipTimer) {
      window.clearTimeout(linkTooltipTimer);
      linkTooltipTimer = 0;
    }
    if (linkTooltip) linkTooltip.style.display = 'none';
  }

  // xterm has no public "drop the hovered link" call; its Linkifier clears on
  // the screen's mouseleave, which is exactly the state wanted here.
  function clearLinkHover(session) {
    if (session.screenEl && session.hoveredLink)
      session.screenEl.dispatchEvent(new MouseEvent('mouseleave'));
    session.hoveredLink = false;
  }

  function clearAllLinkHovers() {
    hideLinkTooltip();
    Object.keys(sessions).forEach(function (key) { clearLinkHover(sessions[key]); });
  }

  function linkHovered(session, event) {
    session.hoveredLink = true;
    setLinkModifier(isLinkModifier(event));
    showLinkTooltip(event);
  }

  function linkLeft(session) {
    session.hoveredLink = false;
    hideLinkTooltip();
  }

  // Plain clicks stay ordinary terminal clicks. The host re-parses `text`,
  // applies its scheme allowlist and resolves paths; the page decides nothing.
  function activateLink(session, event, text) {
    if (!isLinkModifier(event)) return;
    hideLinkTooltip();
    post({ type: 'openLink', id: session.id, text: text, cwd: session.cwd });
  }

  // One logical line -- the hovered row plus the rows it wraps into -- flattened
  // to text with a per-code-unit map back to buffer cells. Wide glyphs own two
  // cells, so ranges cannot be derived from string offsets alone.
  function provideLinks(session, y) {
    var detector = window.FastMDTerminalLinks;
    var buffer = session.term.buffer.active;
    var row = y - 1;
    if (!detector || !buffer.getLine(row)) return undefined;
    var first = row;
    var last = row;
    while (first > 0 && row - first < kLinkRowsAround && buffer.getLine(first).isWrapped) first--;
    while (last - row < kLinkRowsAround && buffer.getLine(last + 1)
           && buffer.getLine(last + 1).isWrapped) last++;

    var text = '';
    var cellX = [];
    var cellY = [];
    var cellW = [];
    var cell = buffer.getNullCell();
    for (var r = first; r <= last; r++) {
      var line = buffer.getLine(r);
      for (var x = 0; x < line.length; x++) {
        if (!line.getCell(x, cell)) break;
        var width = cell.getWidth();
        if (width === 0) continue;
        var chars = cell.getChars() || ' ';
        for (var k = 0; k < chars.length; k++) {
          cellX.push(x);
          cellY.push(r);
          cellW.push(width);
        }
        text += chars;
      }
    }

    var links = [];
    detector.detect(text).forEach(function (found) {
      var s = found.start;
      var e = found.end - 1;
      var range = {
        start: { x: cellX[s] + 1, y: cellY[s] + 1 },
        end: { x: cellX[e] + cellW[e], y: cellY[e] + 1 }
      };
      if (range.start.y > y || range.end.y < y) return;
      links.push({
        range: range,
        text: found.text,
        decorations: { underline: true, pointerCursor: true },
        activate: function (event, linkText) { activateLink(session, event, linkText); },
        hover: function (event) { linkHovered(session, event); },
        leave: function () { linkLeft(session); }
      });
    });
    return links.length ? links : undefined;
  }

  function wireLinks(session) {
    var term = session.term;
    term.registerLinkProvider({
      provideLinks: function (y, callback) { callback(provideLinks(session, y)); }
    });
    // OSC 8 hyperlinks follow the same policy. Non-HTTP schemes are let
    // through so `file://` links from `ls --hyperlink` work; the host's
    // allowlist is what refuses everything else.
    term.options.linkHandler = {
      allowNonHttpProtocols: true,
      activate: function (event, text) { activateLink(session, event, text); },
      hover: function (event) { linkHovered(session, event); },
      leave: function () { linkLeft(session); }
    };
    // Registered after term.open(), so xterm's Linkifier -- on this same
    // element -- has already recorded the press; its mouseup activates the
    // link. Stopping the press here keeps a Ctrl+Click from also starting a
    // selection or reaching a TUI's mouse reporting.
    if (session.screenEl) {
      session.screenEl.addEventListener('mousedown', function (event) {
        if (event.button === 0 && session.hoveredLink && isLinkModifier(event)) {
          event.preventDefault();
          event.stopPropagation();
        }
      });
    }
    // Shell-reported working directory, for relative paths. Without shell
    // integration none of these arrive and the host falls back to the
    // directory the session started in. Returning false leaves the sequence to
    // any other handler.
    var detector = window.FastMDTerminalLinks;
    if (!detector) return;
    function track(parse) {
      return function (data) {
        var cwd = parse(data);
        if (cwd) session.cwd = cwd;
        return false;
      };
    }
    term.parser.registerOscHandler(7, track(detector.cwdFromOsc7));
    term.parser.registerOscHandler(9, track(detector.cwdFromOsc9));
    term.parser.registerOscHandler(633, track(detector.cwdFromOsc633));
  }

  function themeColors() {
    return dark ? {
      background: '#191a1b', foreground: '#bfbfbf', cursor: '#bfbfbf',
      selectionBackground: 'rgba(58, 109, 168, 0.55)',
      black: '#000000', red: '#cd3131', green: '#0dbc79', yellow: '#e5e510',
      blue: '#2472c8', magenta: '#bc3fbc', cyan: '#11a8cd', white: '#e5e5e5',
      brightBlack: '#666666', brightRed: '#f14c4c', brightGreen: '#23d18b',
      brightYellow: '#f5f543', brightBlue: '#3b8eea', brightMagenta: '#d670d6',
      brightCyan: '#29b8db', brightWhite: '#e5e5e5'
    } : {
      background: '#f7f7f7', foreground: '#000000', cursor: '#000000',
      selectionBackground: 'rgba(0, 120, 212, 0.28)',
      black: '#1f1f1f', red: '#c72e0f', green: '#0f7b3d', yellow: '#8a6100',
      blue: '#0451a5', magenta: '#a315a3', cyan: '#006f8f', white: '#4d4d4d',
      brightBlack: '#6e6e6e', brightRed: '#d63a1a', brightGreen: '#087a37',
      brightYellow: '#9a6700', brightBlue: '#0451a5', brightMagenta: '#a315a3',
      brightCyan: '#006f8f', brightWhite: '#707070'
    };
  }

  // xterm 5.3's DOM renderer normally performs this adjustment itself, but
  // some application-provided ANSI styles bypass it. Correct only the visible
  // DOM rows after xterm renders them: this preserves the application's ANSI
  // palette and hierarchy while preventing any text from disappearing into the
  // current terminal background. The terminal has only viewport rows in the
  // DOM, so this never walks its scrollback on output or while typing.
  function cssColor(value) {
    var match = /^rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\)$/.exec(value);
    if (!match) return null;
    return {
      r: Number(match[1]), g: Number(match[2]), b: Number(match[3]),
      a: match[4] === undefined ? 1 : Number(match[4])
    };
  }

  function luminance(color) {
    function channel(value) {
      value /= 255;
      return value <= 0.03928 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
  }

  function contrastRatio(foreground, background) {
    var first = luminance(foreground);
    var second = luminance(background);
    return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
  }

  function composite(foreground, background) {
    var alpha = foreground.a;
    return {
      r: Math.round(background.r + (foreground.r - background.r) * alpha),
      g: Math.round(background.g + (foreground.g - background.g) * alpha),
      b: Math.round(background.b + (foreground.b - background.b) * alpha),
      a: 1
    };
  }

  function contrastSafeColor(foreground, background, requiredRatio) {
    var displayed = composite(foreground, background);
    if (contrastRatio(displayed, background) >= requiredRatio) return null;

    // Preserve the colour's hue as far as possible by moving it only toward
    // the endpoint (black or white) that maximises contrast for this cell.
    var endpoint = luminance(background) > 0.5
      ? { r: 0, g: 0, b: 0, a: 1 }
      : { r: 255, g: 255, b: 255, a: 1 };
    var low = 0;
    var high = 1;
    for (var step = 0; step < 8; ++step) {
      var amount = (low + high) / 2;
      var candidate = {
        r: Math.round(displayed.r + (endpoint.r - displayed.r) * amount),
        g: Math.round(displayed.g + (endpoint.g - displayed.g) * amount),
        b: Math.round(displayed.b + (endpoint.b - displayed.b) * amount), a: 1
      };
      if (contrastRatio(candidate, background) >= requiredRatio) high = amount;
      else low = amount;
    }
    var result = {
      r: Math.round(displayed.r + (endpoint.r - displayed.r) * high),
      g: Math.round(displayed.g + (endpoint.g - displayed.g) * high),
      b: Math.round(displayed.b + (endpoint.b - displayed.b) * high)
    };
    return 'rgb(' + result.r + ', ' + result.g + ', ' + result.b + ')';
  }

  // A viewport holds hundreds of spans but only a handful of distinct colour
  // pairs, and the correction for a pair never changes while the theme stands.
  // Reset by applyTheme(), which is also what changes the page background the
  // corrections are computed against.
  var contrastCorrections = Object.create(null);

  function contrastCorrectionFor(color, background, terminalBackground) {
    var key = color + '|' + background;
    if (key in contrastCorrections) return contrastCorrections[key];
    var foreground = cssColor(color);
    var corrected = null;
    if (foreground) {
      var behind = cssColor(background);
      behind = behind && behind.a > 0 ? composite(behind, terminalBackground) : terminalBackground;
      // A white terminal needs the full target even for SGR dim. xterm's
      // half-target convention remains too pale here, particularly for prose
      // emitted by terminal clients such as agy.
      corrected = contrastSafeColor(foreground, behind, minimumContrastRatio);
    }
    contrastCorrections[key] = corrected;
    return corrected;
  }

  function enforceRenderedContrast(session) {
    var rows = session.element.querySelector('.xterm-rows');
    if (!rows) return;
    // terminal-host is deliberately transparent; the body owns the themed
    // background that xterm's transparent cells are actually composited over.
    var terminalBackground = cssColor(window.getComputedStyle(document.body).backgroundColor);
    if (!terminalBackground) return;
    var spans = rows.querySelectorAll('span:not([data-fastmd-contrast])');
    if (!spans.length) return;
    // Read every computed style before writing any: a style write invalidates
    // the next span's read, so interleaving them turns one style recalculation
    // per render into one per span, on every frame of sustained output.
    var pending = [];
    spans.forEach(function (span) {
      var style = window.getComputedStyle(span);
      pending.push([span, style.color, style.backgroundColor]);
    });
    pending.forEach(function (entry) {
      entry[0].setAttribute('data-fastmd-contrast', '');
      var corrected = contrastCorrectionFor(entry[1], entry[2], terminalBackground);
      if (corrected) entry[0].style.color = corrected;
    });
  }

  function scheduleRenderedContrast(session) {
    // xterm's onRender notification can precede the DOM row replacement (most
    // visibly while scrolling). Defer one frame so we inspect the committed
    // rows, not the rows that are about to be discarded. Coalescing keeps a
    // burst of output to one viewport-sized pass per animation frame.
    if (session.contrastScheduled) return;
    session.contrastScheduled = true;
    window.requestAnimationFrame(function () {
      session.contrastScheduled = false;
      enforceRenderedContrast(session);
    });
  }

  function clearRenderedContrast(session) {
    session.element.querySelectorAll('[data-fastmd-contrast]').forEach(function (span) {
      span.style.removeProperty('color');
      span.removeAttribute('data-fastmd-contrast');
    });
  }

  // Retained so a control sequence split across two host reads is still seen
  // whole. One character less than the longest sequence scanned for would do;
  // the margin costs nothing now that re-scanned ground is skipped.
  var kProtocolTailLength = 15;

  // The retained tail is scanned twice: once as the end of its own chunk and
  // again as the head of the next probe. A sequence lying wholly inside it was
  // therefore already acted on -- only one reaching into the new text is new.
  // Without this, a capability query landing near a chunk boundary would be
  // answered twice, sending the application a duplicate reply.
  function findUnscanned(probe, needle, scanned) {
    for (var at = probe.indexOf(needle); at >= 0; at = probe.indexOf(needle, at + 1)) {
      if (at + needle.length > scanned) return at;
    }
    return -1;
  }

  function encodeUtf8(text) {
    var bytes = new TextEncoder().encode(text);
    var binary = '';
    for (var i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    return btoa(binary);
  }

  function decodeBase64(data) {
    var binary = atob(data);
    var bytes = new Uint8Array(binary.length);
    for (var i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
  }

  function reportSize(session) {
    if (!session || !session.term) return;
    var cols = session.term.cols;
    var rows = session.term.rows;
    if (cols === session.lastCols && rows === session.lastRows) return;
    session.lastCols = cols;
    session.lastRows = rows;
    post({ type: 'resize', id: session.id, cols: cols, rows: rows });
  }

  // ConPTY's Win32 input mode preserves modifier state and virtual-key data
  // that ordinary VT input cannot represent (for example Shift+Enter and
  // Ctrl+Backspace). The mode is negotiated by the child through CSI ?9001h;
  // key presses are then encoded as KEY_EVENT_RECORD-compatible VT sequences.
  function win32KeyEvent(vk, scanCode, unicodeChar, keyDown, controlKeyState) {
    return '\x1b[' + vk + ';' + scanCode + ';' + unicodeChar
      + ';' + (keyDown ? '1' : '0') + ';' + controlKeyState + ';1_';
  }

  function sendWin32KeyPress(session, data) {
    if (!session.win32InputMode) return false;
    post({ type: 'input', id: session.id, data: encodeUtf8(data) });
    return true;
  }

  function kittyKey(session, keyCode, modifiers) {
    if (!session.kittySpeaks) return false;
    // Kitty encodes modifiers as 1 + (shift=1, alt=2, ctrl=4, ...).
    post({ type: 'input', id: session.id,
      data: encodeUtf8('\x1b[' + keyCode + ';' + (1 + modifiers) + 'u') });
    return true;
  }

  function fit(session) {
    if (!session || !session.fitAddon) return;
    // A hidden host still has a real box (visibility, not display), but a panel
    // collapsed to nothing does not -- proposeDimensions returns undefined and
    // fit() would throw.
    try {
      var proposed = session.fitAddon.proposeDimensions();
      if (!proposed || !proposed.cols || !proposed.rows) return;
      if (!isFinite(proposed.cols) || !isFinite(proposed.rows)) return;
      session.fitAddon.fit();
      reportSize(session);
    } catch (e) { /* transient layout state; the resize observer retries */ }
    updateScrollbar(session);
  }

  function fitAll() {
    Object.keys(sessions).forEach(function (key) { fit(sessions[key]); });
  }

  // WebView2's own scrollbar (classic or Fluent) can't give a themed,
  // persistently-visible bar -- see terminal.css. This mirrors it against the
  // real .xterm-viewport scroll state instead.
  function updateScrollbar(session) {
    var viewport = session.viewportEl;
    var thumb = session.scrollbarThumb;
    if (!viewport || !thumb) return;
    var scrollHeight = viewport.scrollHeight;
    var clientHeight = viewport.clientHeight;
    if (scrollHeight <= clientHeight + 1) {
      thumb.style.display = 'none';
      return;
    }
    var thumbHeight = Math.max(24, clientHeight * clientHeight / scrollHeight);
    var scrollable = scrollHeight - clientHeight;
    var top = scrollable > 0 ? (clientHeight - thumbHeight) * (viewport.scrollTop / scrollable) : 0;
    thumb.style.display = 'block';
    thumb.style.height = thumbHeight + 'px';
    thumb.style.top = top + 'px';
  }

  function wireScrollbarDrag(session) {
    session.scrollbarThumb.addEventListener('mousedown', function (event) {
      var viewport = session.viewportEl;
      var thumb = session.scrollbarThumb;
      if (!viewport || event.button !== 0) return;
      event.preventDefault();
      var startY = event.clientY;
      var startScrollTop = viewport.scrollTop;
      var trackRange = viewport.clientHeight - thumb.offsetHeight;
      var scrollRange = viewport.scrollHeight - viewport.clientHeight;
      thumb.classList.add('dragging');
      function onMove(moveEvent) {
        if (trackRange <= 0 || scrollRange <= 0) return;
        var ratio = (moveEvent.clientY - startY) / trackRange;
        viewport.scrollTop = startScrollTop + ratio * scrollRange;
      }
      function onUp() {
        thumb.classList.remove('dragging');
        window.removeEventListener('mousemove', onMove);
        window.removeEventListener('mouseup', onUp);
      }
      window.addEventListener('mousemove', onMove);
      window.addEventListener('mouseup', onUp);
    });
  }

  function createSession(id) {
    if (sessions[id]) return;

    var element = document.createElement('div');
    element.className = 'terminal-host';
    element.setAttribute('data-session', String(id));
    container.appendChild(element);

    var term = new window.Terminal({
      // Prefer the no-ligature Cascadia Mono terminal face under WebView2.
      fontFamily: 'Consolas, "Cascadia Mono", "Cascadia Code", "Courier New", monospace',
      fontSize: fontSize,
      fontWeight: 'normal',
      fontWeightBold: 'bold',
      allowProposedApi: true,
      cursorBlink: true,
      convertEol: false,
      scrollback: 5000,
      // Match VS Code's accessible terminal default. xterm renders SGR dim
      // at 50% opacity; its default contrast ratio of 1 leaves subdued ANSI
      // output (including agy's prose) nearly white on a light background.
      // xterm adjusts dim text against half this target, preserving its visual
      // hierarchy while keeping it legible in both themes.
      minimumContrastRatio: minimumContrastRatio,
      theme: themeColors()
    });

    var fitAddon = new window.FitAddon.FitAddon();
    term.loadAddon(fitAddon);
    try {
      // Correct display widths for CJK and emoji. Without this a wide glyph
      // occupies one cell and the whole line drifts out of alignment.
      var unicode11 = new window.Unicode11Addon.Unicode11Addon();
      term.loadAddon(unicode11);
      term.unicode.activeVersion = '11';
    } catch (e) { /* fall back to the built-in width table */ }

    term.open(element);

    var scrollbar = document.createElement('div');
    scrollbar.className = 'term-scrollbar';
    var scrollbarThumb = document.createElement('div');
    scrollbarThumb.className = 'term-scrollbar-thumb';
    scrollbar.appendChild(scrollbarThumb);
    element.appendChild(scrollbar);

    var session = {
      id: id,
      term: term,
      fitAddon: fitAddon,
      element: element,
      viewportEl: element.querySelector('.xterm-viewport'),
      screenEl: element.querySelector('.xterm-screen'),
      scrollbarThumb: scrollbarThumb,
      decoder: new TextDecoder('utf-8'),
      win32InputMode: false,
      protocolTail: '',
      kittyFlags: 0,
      kittySpeaks: false,
      lastCols: -1,
      lastRows: -1,
      contrastScheduled: false,
      hoveredLink: false,
      cwd: ''
    };
    sessions[id] = session;

    try {
      wireLinks(session);
    } catch (e) { /* links are a nicety, never fail the terminal for them */ }

    wireScrollbarDrag(session);
    if (session.viewportEl)
      session.viewportEl.addEventListener('scroll', function () { updateScrollbar(session); });

    term.onRender(function () { scheduleRenderedContrast(session); updateScrollbar(session); });

    term.onData(function (data) {
      post({ type: 'input', id: id, data: encodeUtf8(data) });
    });

    // Ctrl+C only means "interrupt" when nothing is selected; with a selection
    // it has to copy, which is what every Windows terminal does and what users
    // will otherwise lose text to.
    term.attachCustomKeyEventHandler(function (event) {
      if (event.type !== 'keydown') return true;
      // A bare ESC byte is ambiguous to ConPTY's input parser. Once it has
      // seen any Win32 input-mode sequence (the Shift+Enter and Ctrl+Backspace
      // handlers below send them), ConPTY holds a trailing ESC as the prefix
      // of the next sequence: Escape never arrives and the following key
      // becomes Alt+key. Console-input readers such as crossterm then cannot
      // see Escape at all. A complete KEY_EVENT_RECORD pair is unambiguous,
      // and VT-input readers still receive it as a single 0x1B.
      if (session.win32InputMode && event.key === 'Escape' && !event.isComposing
          && !event.ctrlKey && !event.altKey && !event.metaKey) {
        event.preventDefault();
        event.stopPropagation();
        var escapeState = event.shiftKey ? 16 : 0;
        sendWin32KeyPress(session,
          win32KeyEvent(27, 1, 27, true, escapeState)
          + win32KeyEvent(27, 1, 27, false, escapeState));
        return false;
      }
      // xterm's legacy encoding makes Shift+Enter indistinguishable from
      // Enter. ConPTY's Win32 input mode restores the original KEY_EVENT_RECORD
      // instead, allowing PSReadLine's default AddLine handler to insert a
      // line without submitting the command. Preventing the DOM default is
      // essential: otherwise xterm also sends a plain carriage return.
      if ((session.kittySpeaks || session.win32InputMode)
          && event.shiftKey && !event.ctrlKey && !event.altKey
          && (event.key === 'Enter' || event.key === 'NumpadEnter')) {
        event.preventDefault();
        event.stopPropagation();
        if (kittyKey(session, 13, 1)) return false;
        // Send the same four KEY_EVENT_RECORDs a native Windows terminal
        // produces: Shift down, Enter down/up, then Shift up.
        sendWin32KeyPress(session,
          win32KeyEvent(16, 42, 0, true, 16)
          + win32KeyEvent(13, 28, 13, true, 16)
          + win32KeyEvent(13, 28, 13, false, 16)
          + win32KeyEvent(16, 42, 0, false, 0));
        return false;
      }
      if ((session.kittySpeaks || session.win32InputMode)
          && event.ctrlKey && !event.shiftKey && !event.altKey
          && (event.key === 'Backspace' || event.code === 'Backspace')) {
        event.preventDefault();
        event.stopPropagation();
        // agy's line editor follows the Unix terminal convention: Ctrl+W is
        // backward-kill-word. Its current Kitty decoder does not map the
        // otherwise-canonical CSI 127;5u Backspace event to that operation.
        if (session.kittySpeaks) {
          post({ type: 'input', id: session.id, data: encodeUtf8('\x17') });
          return false;
        }
        // Send Ctrl down, Backspace down/up, then Ctrl up. PSReadLine uses
        // this native combination for backward word deletion.
        sendWin32KeyPress(session,
          win32KeyEvent(17, 29, 0, true, 8)
          + win32KeyEvent(8, 14, 0, true, 8)
          + win32KeyEvent(8, 14, 0, false, 8)
          + win32KeyEvent(17, 29, 0, false, 0));
        return false;
      }
      if ((session.kittySpeaks || session.win32InputMode)
          && event.ctrlKey && !event.shiftKey && !event.altKey
          && (event.key === 'Delete' || event.code === 'Delete')) {
        event.preventDefault();
        event.stopPropagation();
        // The forward counterpart to Ctrl+W is Meta+D (ESC d) in the Unix
        // terminal line-editing convention. agy's current Kitty decoder does
        // not turn Ctrl+Delete into that operation, so preserve its expected
        // byte sequence when it has opted into Kitty input.
        if (session.kittySpeaks) {
          post({ type: 'input', id: session.id, data: encodeUtf8('\x1bd') });
          return false;
        }
        // PSReadLine recognizes the native Ctrl+Delete KEY_EVENT_RECORDs.
        sendWin32KeyPress(session,
          win32KeyEvent(17, 29, 0, true, 8)
          + win32KeyEvent(46, 83, 0, true, 8)
          + win32KeyEvent(46, 83, 0, false, 8)
          + win32KeyEvent(17, 29, 0, false, 0));
        return false;
      }
      if (event.ctrlKey && !event.altKey && event.key === 'c' && term.hasSelection()) {
        event.preventDefault();
        post({ type: 'copy', text: term.getSelection() });
        term.clearSelection();
        return false;
      }
      if (event.ctrlKey && event.shiftKey && (event.key === 'C' || event.key === 'c')) {
        event.preventDefault();
        if (term.hasSelection()) post({ type: 'copy', text: term.getSelection() });
        return false;
      }
      // Plain Ctrl+V (and Shift+Insert) must be handled here, not left to the
      // DOM 'paste' event: AreBrowserAcceleratorKeysEnabled is FALSE so that
      // Ctrl+P/Ctrl+S/F5 reach the shell instead of Chromium, and that same
      // flag stops Chromium synthesising a paste event -- Ctrl+V would
      // otherwise arrive at the shell as a literal ^V.
      // preventDefault matters as much as the post: returning false only tells
      // xterm to skip the key, and Chromium would still run its own default
      // paste afterwards -- which inserted the clipboard text twice.
      if (event.ctrlKey && !event.altKey && (event.key === 'v' || event.key === 'V')) {
        event.preventDefault();
        post({ type: 'requestPaste', id: id });
        return false;
      }
      if (event.shiftKey && event.key === 'Insert') {
        event.preventDefault();
        post({ type: 'requestPaste', id: id });
        return false;
      }
      return true;
    });

    // Right-click belongs to the host's copy/paste gesture. Consume its press,
    // drag and release before xterm can report them to a mouse-enabled console
    // application: it may paste from the clipboard on the right-button record,
    // then receive our contextmenu paste as well. This depends on console modes
    // (not the shell/CLI name). Leave all other mouse input to xterm.
    function consumeRightButton(event) {
      if (event.type === 'mousemove' ? !(event.buttons & 2) : event.button !== 2) return;
      event.preventDefault();
      event.stopPropagation();
      if (event.type === 'mousedown') term.focus();
    }
    element.addEventListener('mousedown', consumeRightButton, true);
    element.addEventListener('mousemove', consumeRightButton, true);
    element.addEventListener('mouseup', consumeRightButton, true);

    // Right-click copies an existing selection, otherwise pastes.
    element.addEventListener('contextmenu', function (event) {
      event.preventDefault();
      if (term.hasSelection()) {
        post({ type: 'copy', text: term.getSelection() });
        term.clearSelection();
      } else {
        post({ type: 'requestPaste', id: id });
      }
    });

    fit(session);
  }

  function destroySession(id) {
    var session = sessions[id];
    if (!session) return;
    try { session.term.dispose(); } catch (e) { /* already gone */ }
    if (session.element && session.element.parentNode)
      session.element.parentNode.removeChild(session.element);
    delete sessions[id];
    if (activeId === id) activeId = -1;
  }

  function showSession(id) {
    activeId = id;
    Object.keys(sessions).forEach(function (key) {
      var session = sessions[key];
      var isActive = session.id === id;
      if (!isActive) clearLinkHover(session);
      session.element.classList.toggle('active', isActive);
      if (isActive) {
        // The panel may have been resized while this session was hidden.
        fit(session);
        try { session.term.focus(); } catch (e) { /* not attached yet */ }
      }
    });
  }

  function writeOutput(id, base64) {
    var session = sessions[id];
    if (!session) return;
    // stream:true is what keeps a UTF-8 sequence split across two host reads
    // from being decoded as two replacement characters.
    var text = session.decoder.decode(decodeBase64(base64), { stream: true });
    // ConPTY asks its terminal host for Win32 input mode by emitting this
    // private-mode sequence on the output stream during startup. Detect it
    // before xterm parses the chunk; a chunk boundary may split the sequence.
    var protocolProbe = session.protocolTail + text;
    var scanned = session.protocolTail.length;
    if (findUnscanned(protocolProbe, '\x1b[?9001h', scanned) >= 0)
      session.win32InputMode = true;
    // Modern TUIs such as agy use the Kitty keyboard protocol. `CSI ? u` asks
    // the terminal to report its currently enabled flags: answer with what is
    // actually enabled, not a blanket 31. Claiming all five enhancements tells
    // the application to expect every key as a CSI u sequence, which this page
    // does not send -- only the few combinations handled below.
    //
    // The query is still what marks the peer as kitty-speaking (kittySpeaks),
    // which is what those handlers key off; an explicit pop takes it back.
    if (findUnscanned(protocolProbe, '\x1b[?u', scanned) >= 0) {
      post({ type: 'input', id: id,
        data: encodeUtf8('\x1b[?' + session.kittyFlags + 'u') });
      session.kittySpeaks = true;
    }
    var kittyRequest;
    var kittyRequests = /\x1b\[([>=])(\d+)(?:;(\d))?u/g;
    while ((kittyRequest = kittyRequests.exec(protocolProbe)) !== null) {
      if (kittyRequests.lastIndex <= scanned) continue;
      session.kittyFlags = Number(kittyRequest[2]);
      session.kittySpeaks = true;
    }
    var kittyPop;
    var kittyPops = /\x1b\[<\d*u/g;
    while ((kittyPop = kittyPops.exec(protocolProbe)) !== null) {
      if (kittyPops.lastIndex <= scanned) continue;
      session.kittyFlags = 0;
      session.kittySpeaks = false;
    }
    session.protocolTail = protocolProbe.slice(-kProtocolTailLength);
    if (text) session.term.write(text);
  }

  function applyTheme(isDark, size) {
    dark = !!isDark;
    if (size) fontSize = size;
    document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light');
    document.documentElement.style.setProperty('--fs', fontSize + 'px');
    var colors = themeColors();
    contrastCorrections = Object.create(null);
    Object.keys(sessions).forEach(function (key) {
      var session = sessions[key];
      clearRenderedContrast(session);
      session.term.options.theme = colors;
      session.term.options.fontSize = fontSize;
      fit(session);
    });
  }

  if (host) {
    host.addEventListener('message', function (event) {
      var message = event.data;
      if (!message || !message.type) return;
      switch (message.type) {
        case 'create':    createSession(message.id); break;
        case 'destroy':   destroySession(message.id); break;
        case 'show':      showSession(message.id); break;
        case 'output':    writeOutput(message.id, message.data); break;
        case 'theme':     applyTheme(message.dark, message.fs); break;
        case 'clear':
          if (sessions[message.id]) sessions[message.id].term.clear();
          break;
        case 'copy':
          if (sessions[message.id] && sessions[message.id].term.hasSelection())
            post({ type: 'copy', text: sessions[message.id].term.getSelection() });
          break;
        case 'paste':
          if (sessions[message.id])
            sessions[message.id].term.paste(message.text || '');
          break;
        case 'selectAll':
          if (sessions[message.id]) sessions[message.id].term.selectAll();
          break;
        default: break;
      }
    });
  }

  // Fallback only: with browser accelerator keys disabled Chromium does not
  // synthesise a paste event for Ctrl+V (that is handled above), but a
  // context-menu or middle-click paste still arrives here, and clipboardData
  // needs no permission prompt the way navigator.clipboard.readText() does.
  document.addEventListener('paste', function (event) {
    if (activeId < 0 || !sessions[activeId]) return;
    var text = event.clipboardData && event.clipboardData.getData('text');
    if (!text) return;
    event.preventDefault();
    post({ type: 'input', id: activeId, data: encodeUtf8(text) });
  });

  window.addEventListener('resize', fitAll);

  // Capture phase: xterm handles keys on its textarea and may stop them.
  function trackLinkModifier(event) { setLinkModifier(isLinkModifier(event)); }
  window.addEventListener('keydown', trackLinkModifier, true);
  window.addEventListener('keyup', trackLinkModifier, true);
  window.addEventListener('mousemove', trackLinkModifier, true);
  window.addEventListener('blur', function () {
    setLinkModifier(false);
    clearAllLinkHovers();
  });
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) clearAllLinkHovers();
  });
  if (window.ResizeObserver) new ResizeObserver(fitAll).observe(container);

  // Seed theme/size from the query string so the very first paint is already
  // correct, the same bootstrap the preview shell uses.
  (function bootstrap() {
    var params = new URLSearchParams(window.location.search);
    var size = parseInt(params.get('fs'), 10);
    // No ?fs= means the host had nothing stored; fall back to this module's
    // own default rather than a second literal that can drift from it.
    applyTheme(params.get('theme') === 'dark', isFinite(size) && size > 0 ? size : fontSize);
  })();

  function signalReady() { post({ type: 'ready' }); }
  if (document.fonts && document.fonts.load && document.fonts.ready) {
    document.fonts.ready.then(signalReady, signalReady);
  } else {
    signalReady();
  }
})();
