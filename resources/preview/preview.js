/* FastMD WebView2 preview controller.
 *
 * Message protocol (host C++ <-> page):
 *   Host -> page  (chrome.webview message, JSON objects):
 *     { type: 'content',        html, seq, blocks? } set body, highlight+typeset
 *     { type: 'patch', seq, base, oldCount, blocks, shift?, moved?, ops }
 *                                                change only some blocks (below)
 *     { type: 'theme',          dark, fs }      switch theme / font size
 *     { type: 'font',           family, faces } body font-family ('' = stylesheet's) + @font-face rules
 *     { type: 'scrollToFraction', fraction }    programmatic scroll (sync)
 *     { type: 'scrollToAnchor', id }            outline / heading navigation
 *     { type: 'scrollToSourceLine', line }      one-time post-restore sync
 *   Page -> host  (chrome.webview.postMessage, JSON objects):
 *     { type: 'ready' }                         shell finished loading
 *     { type: 'rendered', seq }                 content displayed (bench uses this)
 *     { type: 'resync', seq }                   patch refused: send the whole body
 *     { type: 'blocksMismatch', seq }           host's block split != this DOM
 *     { type: 'scroll', fraction }              user scrolled
 *     { type: 'openExternal', url }             link click -> open in browser
 *     { type: 'linkHover', url }                link destination entered/left
 *     { type: 'zoom', delta }                   Ctrl+wheel over the preview
 *     { type: 'outlineClicked' }                Outline button clicked
 *     { type: 'previewFlash', line }            double-click on mapped content
 *     { type: 'contextMenu', x, y, hasSelection } right-click inside the Preview
 *
 *   Additional host -> page message:
 *     { type: 'outlinePopupOpen', open }        force the Outline button's
 *                                                expanded/highlighted state
 *                                                while the (natively hosted)
 *                                                outline popup is showing
 */
(function () {
  "use strict";

  var content = document.getElementById('content');
  var host = (window.chrome && window.chrome.webview) ? window.chrome.webview : null;
  var standalone = document.documentElement.getAttribute('data-fastmd-standalone') === '1';
  var hostBridge = !!host && !standalone;

  function post(obj) { if (hostBridge) host.postMessage(obj); }

  var assetPromises = {};

  function loadScript(src) {
    if (assetPromises[src]) return assetPromises[src];
    if (document.querySelector('script[data-fastmd-asset="' + src + '"]'))
      return Promise.resolve();
    assetPromises[src] = new Promise(function (resolve, reject) {
      var script = document.createElement('script');
      script.src = src;
      script.onload = resolve;
      script.onerror = reject;
      document.head.appendChild(script);
    });
    return assetPromises[src];
  }

  function loadStyle(id, href) {
    if (document.getElementById(id)) return;
    var link = document.createElement('link');
    link.id = id;
    link.rel = 'stylesheet';
    link.href = href;
    document.head.appendChild(link);
  }

  function setHighlightTheme(dark) {
    var lightCss = document.getElementById('hljs-light');
    var darkCss = document.getElementById('hljs-dark');
    // WebView2 can retain a stylesheet enabled when its disabled state changes
    // while it is loading.  A non-matching media query reliably removes it from
    // the cascade, preventing dark token colors on a light preview.
    if (lightCss) lightCss.media = dark ? 'not all' : 'all';
    if (darkCss) darkCss.media = dark ? 'all' : 'not all';
  }

  // A feature is never unloaded once its assets are in the page, so each of these
  // four whole-body regexes only has to run until it first matches. On a document
  // that uses code, math and Mermaid, later pushes scan the body zero times.
  var featureLoaded = { code: false, math: false, chemistry: false, mermaid: false };

  function requiredFeatures(html) {
    // Unlabelled fences are never highlighted (highlightIn skips them) and Mermaid
    // fences become diagrams, so only another labelled block needs highlight.js.
    if (!featureLoaded.code) featureLoaded.code = /<pre\b[^>]*>\s*<code\b[^>]*\blanguage-(?!mermaid\b)/i.test(html);
    if (!featureLoaded.math) featureLoaded.math = /\$\$|\\\[|\\\(|\$(?:[^$\n]|\\\$)+\$/.test(html);
    if (!featureLoaded.chemistry) featureLoaded.chemistry = /\\ce\s*\{|\\pu\s*\{/.test(html);
    if (!featureLoaded.mermaid) featureLoaded.mermaid = /language-mermaid\b/i.test(html);
    return featureLoaded;
  }

  function ensureFeatures(html) {
    var features = requiredFeatures(html);
    var loads = [];
    if (features.code) {
      loadStyle('hljs-light', 'highlight/github.min.css');
      loadStyle('hljs-dark', 'highlight/github-dark.min.css');
      setHighlightTheme(document.documentElement.getAttribute('data-theme') === 'dark');
      loads.push(loadScript('highlight/highlight.min.js'));
    }
    if (features.math || features.chemistry) {
      loadStyle('katex-css', '../katex/katex.min.css');
      var mathLoad = loadScript('../katex/katex.min.js');
      if (features.chemistry) mathLoad = mathLoad.then(function () {
        return loadScript('../katex/contrib/mhchem.min.js');
      });
      loads.push(mathLoad.then(function () { return loadScript('../katex/contrib/auto-render.min.js'); }));
    }
    if (features.mermaid) {
      loads.push(loadScript('mermaid/mermaid.min.js').then(function () {
        mermaidReady = !!window.mermaid;
        initMermaid(document.documentElement.getAttribute('data-theme') === 'dark');
      }));
    }
    return Promise.all(loads).catch(function () {});
  }

  // Heading sizes and margins are derived from --fs in preview.css.
  function applyBaseFontSize(fs) {
    document.documentElement.style.setProperty('--fs', fs + 'px');
  }

  // -------------------------------------------------------------------------
  // Mermaid — offline diagram rendering. Initialized once here; re-themed (and
  // re-rendered) only when the light/dark theme actually flips.
  // -------------------------------------------------------------------------
  var mermaidReady = false;
  var mermaidDark = document.documentElement.getAttribute('data-theme') === 'dark';
  var mermaidCounter = 0;

  function mermaidTheme(dark) { return dark ? 'dark' : 'default'; }

  function initMermaid(dark) {
    if (!mermaidReady) return;
    // securityLevel 'strict' sanitizes labels and disables interactivity — the
    // right posture for rendering untrusted local Markdown offline.
    // suppressErrorRendering keeps Mermaid from injecting its own error graphic
    // into the DOM so we can show a clean inline error instead.
    window.mermaid.initialize({
      startOnLoad: false,
      securityLevel: 'strict',
      suppressErrorRendering: true,
      theme: mermaidTheme(dark),
      fontFamily: "'Segoe UI',system-ui,-apple-system,sans-serif"
    });
  }

  // Replace every ```mermaid fenced block with its rendered SVG. Runs before
  // highlight.js so diagram source is never syntax-highlighted. Async because
  // mermaid.render returns a promise.
  function renderMermaid(token, root) {
    if (!mermaidReady) return Promise.resolve();
    var nodes = (root || content).querySelectorAll('pre > code.language-mermaid');
    if (!nodes.length) return Promise.resolve();

    var jobs = [];
    for (var i = 0; i < nodes.length; i++) {
      var pre = nodes[i].parentNode;
      if (!pre || !pre.parentNode) continue;
      jobs.push({ pre: pre, src: nodes[i].textContent });  // decoded (entities -> chars)
    }
    if (!jobs.length) return Promise.resolve();

    // A %%{init: ...}%% directive mutates Mermaid's module-global config for the
    // duration of the render that carries it, so a batch holding one has to stay
    // serial: overlapping renders would let that config leak onto a neighbouring
    // diagram. With no directive in the batch every render sees the same config
    // and they can safely overlap -- which is where the win is, since each one
    // awaits diagram-module loading and font measurement, and those gaps are
    // what serialization was paying for once per diagram.
    if (jobs.some(function (job) { return /%%\{/.test(job.src); })) {
      var chain = Promise.resolve();
      jobs.forEach(function (job) {
        chain = chain.then(function () { return renderMermaidJob(token, job); });
      });
      return chain;
    }
    return Promise.all(jobs.map(function (job) { return renderMermaidJob(token, job); }));
  }

  // One ```mermaid block: render it, then swap the <pre> for the diagram. Each
  // job owns its own nodes, so concurrent jobs never touch the same DOM.
  function renderMermaidJob(token, job) {
    if (token !== renderToken) return Promise.resolve();   // superseded by newer content
    var pre = job.pre;
    if (!pre.parentNode) return Promise.resolve();
    var container = document.createElement('div');
    container.className = 'mermaid-diagram';
    // Mermaid replaces the mapped <pre>; retain its source provenance on the
    // replacement so source-aware operations continue to work.
    ['data-src-start', 'data-src-end'].forEach(function (name) {
      var value = pre.getAttribute(name);
      if (value !== null) container.setAttribute(name, value);
    });
    return renderOneDiagram(container, job.src).then(function () {
      if (token !== renderToken) return;
      if (pre.parentNode) pre.parentNode.replaceChild(container, pre);
    });
  }

  // Render a single diagram into `container` from Mermaid source. On success the
  // source is stashed on the node (__mermaidSrc) so a later theme flip can
  // re-render it. On failure a themed inline error replaces the diagram; the
  // rest of the document is untouched.
  function renderOneDiagram(container, src) {
    var id = 'fastmd-mmd-' + (mermaidCounter++);
    return window.mermaid.render(id, src).then(function (res) {
      container.innerHTML = res.svg;
      container.__mermaidSrc = src;
      if (hostBridge) {
        makeDiagramInteractive(container);
      } else {
        container.classList.add('mermaid-saveable');
        container.title = 'Right-click to save image';
      }
    }).catch(function (err) {
      container.className = 'mermaid-error';
      var msg = (err && err.message) ? err.message : String(err);
      var title = document.createElement('div');
      title.className = 'mermaid-error-title';
      title.textContent = 'Mermaid diagram error';
      var body = document.createElement('pre');
      body.className = 'mermaid-error-msg';
      body.textContent = msg;
      container.innerHTML = '';
      container.appendChild(title);
      container.appendChild(body);
      // Mermaid can leave a stray measurement node in <body> on failure.
      var orphan = document.getElementById('d' + id) || document.getElementById(id);
      if (orphan && orphan.parentNode) orphan.parentNode.removeChild(orphan);
    });
  }

  // Re-render already-drawn diagrams after a light/dark flip (Mermaid bakes
  // colors into the SVG, so a restyle is not enough). Swapping innerHTML in one
  // shot keeps this flash-free.
  function rerenderMermaidForTheme(token) {
    if (!mermaidReady) return;
    var diagrams = content.querySelectorAll('.mermaid-diagram');
    diagrams.forEach(function (container) {
      var src = container.__mermaidSrc;
      if (!src) return;
      window.mermaid.render('fastmd-mmd-' + (mermaidCounter++), src).then(function (res) {
        if (token !== renderToken) return;
        container.innerHTML = res.svg;
        makeDiagramInteractive(container);
      }).catch(function () { /* keep the previously rendered diagram on failure */ });
    });
  }

  function setTheme(dark, fs) {
    document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light');
    setHighlightTheme(dark);
    if (typeof fs === 'number' && fs > 0) applyBaseFontSize(fs);

    if (mermaidReady && dark !== mermaidDark) {
      mermaidDark = dark;
      initMermaid(dark);
      rerenderMermaidForTheme(renderToken);
      syncModalTheme();
    }
  }

  // An inline style on <body> outranks preview.css's body rule and leaves the
  // code, KaTeX and Mermaid rules (which name their own fonts) alone. The host
  // sends this only when the family changes, since any change reflows the page.
  // A value the browser rejects is simply not applied. `faces` holds the
  // @font-face rules the stack names (FontPreferences::vietnameseFallbackFaces)
  // and is empty when it names none.
  function setBodyFont(family, faces) {
    var style = document.getElementById('fastmd-font-faces');
    if (faces) {
      if (!style) {
        style = document.createElement('style');
        style.id = 'fastmd-font-faces';
        document.head.appendChild(style);
      }
      if (style.textContent !== faces) style.textContent = faces;
    } else if (style) {
      style.remove();
    }
    document.body.style.fontFamily = family;
  }

  // Document Spacing: custom properties on :root, set only for a non-default
  // preset (an empty value removes the property, so preview.css's own fallback,
  // the default, applies). Nothing else changes: no content is re-sent, and the
  // block diff, line attributes and scroll fraction are layout-independent.
  function setDocumentSpacing(lh, pspace, ligap) {
    var root = document.documentElement.style;
    [['--lh', lh], ['--pspace', pspace], ['--li-gap', ligap]].forEach(function (entry) {
      if (entry[1]) root.setProperty(entry[0], entry[1]);
      else root.removeProperty(entry[0]);
    });
  }

  var mathDelimiters = [
    { left: '$$', right: '$$', display: true },
    { left: '\\[', right: '\\]', display: true },
    { left: '\\(', right: '\\)', display: false },
    { left: '$', right: '$', display: false }
  ];

  function renderDisplayMathFallback(root) {
    if (!window.katex || !root) return;
    // Every rewrite below requires a display-math opener somewhere in the text.
    // One textContent read is far cheaper than querying every <p> and then
    // collecting every text node in the subtree into an array.
    var probe = root.textContent || '';
    if (probe.indexOf('$$') < 0 && probe.indexOf('\\[') < 0) return;
    // Auto-render normally handles this, but MD4C can leave a multi-line
    // display block as one paragraph after callout transformation.  Render
    // from the paragraph's complete text instead of relying on individual
    // text-node boundaries.
    var paragraphs = selfAndDescendants(root, 'p');
    for (var p = 0; p < paragraphs.length; p++) {
      var paragraph = paragraphs[p];
      if (paragraph.querySelector('pre, code, .katex')) continue;
      var paragraphValue = paragraph.textContent.trim();
      var paragraphOpen = paragraphValue.indexOf('$$') === 0 ? '$$'
        : paragraphValue.indexOf('\\[') === 0 ? '\\[' : '';
      var paragraphClose = paragraphOpen === '$$' ? '$$'
        : paragraphOpen === '\\[' ? '\\]' : '';
      if (!paragraphOpen || paragraphValue.slice(-paragraphClose.length) !== paragraphClose
          || paragraphValue.length <= paragraphOpen.length + paragraphClose.length) continue;
      var paragraphHost = document.createElement('span');
      paragraphHost.className = 'fastmd-math-fallback';
      try {
        window.katex.render(paragraphValue.slice(paragraphOpen.length, -paragraphClose.length),
          paragraphHost, { displayMode: true, throwOnError: false });
        paragraph.replaceChildren(paragraphHost);
      } catch (e) {}
    }
    var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    var nodes = [];
    var node;
    while ((node = walker.nextNode())) nodes.push(node);
    nodes.forEach(function (textNode) {
      if (!textNode.parentNode || textNode.parentNode.closest('pre, code, .katex')) return;
      var value = textNode.nodeValue.trim();
      var open = value.indexOf('$$') === 0 ? '$$' : value.indexOf('\\[') === 0 ? '\\[' : '';
      var close = open === '$$' ? '$$' : open === '\\[' ? '\\]' : '';
      if (!open || value.slice(-close.length) !== close || value.length <= open.length + close.length)
        return;
      var host = document.createElement('span');
      host.className = 'fastmd-math-fallback';
      try {
        window.katex.render(value.slice(open.length, -close.length), host,
          { displayMode: true, throwOnError: false });
        textNode.parentNode.replaceChild(host, textNode);
      } catch (e) {}
    });
  }

  // Bumped on every content push. Async mermaid renders check it so a stale
  // in-flight render never writes into DOM that a newer update has replaced,
  // and only the newest render reports 'rendered' to the host.
  var renderToken = 0;

  function reportRendered(seq) {
    // Report after the frame is actually laid out/painted, so the benchmark
    // 'total' includes real display cost, not just DOM assignment.
    requestAnimationFrame(function () {
      requestAnimationFrame(function () {
        if (!hostBridge) {
          window.fastmdPreviewRendered = true;
          window.dispatchEvent(new Event('fastmd-preview-rendered'));
        }
        post({ type: 'rendered', seq: seq });
      });
    });
  }

  // Fenced code block language badge decorator.
  function renderCodeBadges(root) {
    var codeBlocks = (root || content).querySelectorAll('pre > code');
    for (var i = 0; i < codeBlocks.length; i++) {
      var code = codeBlocks[i];
      var pre = code.parentNode;
      if (!pre || pre.tagName !== 'PRE') continue;

      var lang = '';
      var classes = code.className.split(/\s+/);
      for (var j = 0; j < classes.length; j++) {
        if (classes[j].indexOf('language-') === 0) {
          lang = classes[j].substring(9);
          break;
        }
      }

      if (!lang && pre.getAttribute('data-lang')) {
        lang = pre.getAttribute('data-lang');
      }

      if (!lang || lang.toLowerCase() === 'mermaid') continue;

      var displayLang = lang.trim().toUpperCase();
      if (!displayLang) continue;

      var wrapper = pre.parentNode;
      if (!wrapper || !wrapper.classList.contains('code-block-wrapper')) {
        wrapper = document.createElement('div');
        wrapper.className = 'code-block-wrapper';
        pre.parentNode.insertBefore(wrapper, pre);
        wrapper.appendChild(pre);
      }

      var badge = wrapper.querySelector('.code-lang-badge');
      if (!badge) {
        badge = document.createElement('span');
        badge.className = 'code-lang-badge';
        badge.setAttribute('aria-hidden', 'true');
        wrapper.insertBefore(badge, pre);
      }
      badge.textContent = displayLang;
    }
  }

  // Everything below implements incremental rendering. Typing changes one block
  // of a document but the host re-sends the whole body, so replacing
  // content.innerHTML would re-layout, re-highlight, re-typeset and re-draw every
  // diagram in the document on each keystroke pause. Instead the body is split
  // into top-level blocks, diffed against the previous push, and only the changed
  // run is swapped in and decorated.
  //
  // The invariant that makes this safe: one block == one top-level element child
  // of #content, in order. Every decorator preserves that (the badge wrapper
  // replaces a <pre> with its wrapper; Mermaid replaces a <pre> with its diagram
  // container), and a mismatch falls back to a full re-render.
  var prevBlocks = [];

  function selfAndDescendants(root, selector) {
    var out = [];
    if (root.matches && root.matches(selector)) out.push(root);
    var found = root.querySelectorAll ? root.querySelectorAll(selector) : [];
    for (var i = 0; i < found.length; i++) out.push(found[i]);
    return out;
  }

  // Returns the body's top-level elements, or null if it holds bare top-level
  // text (which would break the one-block-per-element-child invariant).
  function splitBlocks(html) {
    var tpl = document.createElement('template');
    tpl.innerHTML = html;
    var out = [];
    for (var n = tpl.content.firstChild; n; n = n.nextSibling) {
      if (n.nodeType === 1) out.push(n);
      else if (n.nodeType === 3 && n.nodeValue.trim() !== '') return null;
    }
    return out;
  }

  function highlightIn(root) {
    if (!window.hljs) return;
    var blocks = selfAndDescendants(root, 'pre code');
    for (var i = 0; i < blocks.length; i++) {
      // An unlabelled fence is intentionally plain code.  Letting highlight.js
      // auto-detect it turns normal prose into spurious languages (and causes
      // renderCodeBadges() to display that guess as the source language).
      var match = /(?:^|\s)language-([^\s]+)/.exec(blocks[i].className);
      if (!match || !match[1]) continue;
      try { window.hljs.highlightElement(blocks[i]); } catch (e) {}
    }
  }

  function renderMathIn(root) {
    if (!window.renderMathInElement) {
      renderDisplayMathFallback(root);
      return;
    }
    // Display blocks in callouts are materialized by renderDisplayMathFallback()
    // first.  Keep that host opaque to the second auto-render pass; otherwise
    // auto-render can walk KaTeX's generated text nodes and partially
    // reinterpret the already-rendered expression.
    var mathOptions = {
      delimiters: mathDelimiters,
      throwOnError: false,
      ignoredClasses: ['fastmd-math-fallback']
    };
    // The wider pass below covers the blockquotes too, so anything the quoted
    // pass has already typeset is marked and excluded from it — the second walk
    // over those subtrees had nothing left to find.
    var rootOptions = {
      delimiters: mathDelimiters,
      throwOnError: false,
      ignoredClasses: ['fastmd-math-fallback', 'fastmd-math-done']
    };
    // Render quoted display paragraphs first.  They can contain a multiline
    // $$...$$ placeholder inside a callout paragraph; letting the wider
    // auto-render pass see it first may split the text around inline delimiters
    // and leave only the final exponent styled.
    var quotedScopes = selfAndDescendants(root, 'blockquote');
    for (var q = 0; q < quotedScopes.length; q++) {
      renderDisplayMathFallback(quotedScopes[q]);
      try { window.renderMathInElement(quotedScopes[q], mathOptions); }
      catch (e) { console.error('[fastmd] KaTeX auto-render failed (blockquote)', e); }
      quotedScopes[q].classList.add('fastmd-math-done');
    }
    try { window.renderMathInElement(root, quotedScopes.length ? rootOptions : mathOptions); }
    catch (e) { console.error('[fastmd] KaTeX auto-render failed', e); }
    // The marker is per-pass state, not part of the rendered document: a later
    // push that re-decorates the same node must not find it already excluded.
    for (var d = 0; d < quotedScopes.length; d++) {
      quotedScopes[d].classList.remove('fastmd-math-done');
      if (!quotedScopes[d].getAttribute('class')) quotedScopes[d].removeAttribute('class');
    }
  }

  // Mermaid first so it strips ```mermaid blocks before highlight.js sees them.
  function decorate(roots, token) {
    var chain = Promise.resolve();
    roots.forEach(function (root) {
      chain = chain.then(function () {
        if (token !== renderToken) return;
        return renderMermaid(token, root);
      });
    });
    return chain.then(function () {
      if (token !== renderToken) return;
      roots.forEach(function (root) {
        if (!root.isConnected) return;
        highlightIn(root);
        renderCodeBadges(root);
        renderMathIn(root);
      });
    });
  }

  // Every attribute that holds an absolute source line: the preview mapping,
  // plus a task item's range (what a checkbox toggle edits).
  var LINE_ATTRS = ['data-src-start', 'data-src-end', 'data-src-line',
                    'data-fastmd-task-start', 'data-fastmd-task-end'];
  var LINE_ATTR_SELECTOR = LINE_ATTRS.map(function (n) { return '[' + n + ']'; }).join(',');
  var LINE_ATTR_PREFIXES = LINE_ATTRS.map(function (n) { return ' ' + n + '="'; });

  // Index of the value of the next line attribute at or after `from` in a
  // serialized block, or -1. Text serializes its quotes unescaped, so prose
  // that reads ` data-src-line="5"` only counts when it sits inside a tag;
  // otherwise it stays content and has to match exactly.
  function nextLineValue(s, from) {
    for (var k = s.indexOf(' data-', from); k >= 0; k = s.indexOf(' data-', k + 6)) {
      for (var n = 0; n < LINE_ATTR_PREFIXES.length; n++) {
        if (s.startsWith(LINE_ATTR_PREFIXES[n], k)) {
          if (s.lastIndexOf('<', k) > s.lastIndexOf('>', k)) return k + LINE_ATTR_PREFIXES[n].length;
          break;
        }
      }
    }
    return -1;
  }

  // Parses the digits of a line attribute's value at `at`: [value, end past
  // the closing quote], or null when it is not a plain number.
  function readLineValue(s, at) {
    var q = at, v = 0;
    for (var c = s.charCodeAt(q); c >= 48 && c <= 57 && q - at < 9; c = s.charCodeAt(++q))
      v = v * 10 + (c - 48);
    if (q === at || s.charCodeAt(q) !== 34) return null;
    return [v, q + 1];
  }

  // One edit moves every source line at or after some line T by one delta and
  // leaves every line before T alone. T itself is not known, only bounded: it
  // lies above every number that stayed and at or below every number that
  // moved. A block's numbers need not all move -- a list whose reference link
  // is defined above the edit, or the footnote list, holds both kinds -- but
  // all of them must fit this one model.
  function newShiftModel() { return { delta: null, maxStayed: -1, minMoved: Infinity }; }

  var BLOCK_SAME = 0, BLOCK_MOVED = 1, BLOCK_DIFFERENT = 2;

  // Compares two serialized blocks with their line-attribute values left out.
  // BLOCK_DIFFERENT unless everything else is byte-identical and every line
  // number either stayed or moved consistently with `model`. On BLOCK_MOVED
  // the model takes on this block's constraints when `commit` is set.
  function matchBlock(a, b, model, commit) {
    if (a === b) return BLOCK_SAME;
    var i = 0, j = 0;
    var delta = model.delta, maxStayed = model.maxStayed, minMoved = model.minMoved;
    for (;;) {
      var ia = nextLineValue(a, i);
      var ib = nextLineValue(b, j);
      if (ia < 0 || ib < 0) {
        if (ia !== ib || a.length - i !== b.length - j || a.substring(i) !== b.substring(j))
          return BLOCK_DIFFERENT;
        break;
      }
      // The text up to each value includes the attribute name, so both sides
      // must be at the same attribute of the same markup.
      if (ia - i !== ib - j || a.substring(i, ia) !== b.substring(j, ib)) return BLOCK_DIFFERENT;
      var ra = readLineValue(a, ia), rb = readLineValue(b, ib);
      if (!ra || !rb) return BLOCK_DIFFERENT;
      if (ra[0] === rb[0]) {
        if (ra[0] >= minMoved) return BLOCK_DIFFERENT;
        if (ra[0] > maxStayed) maxStayed = ra[0];
      } else {
        if (delta === null) delta = rb[0] - ra[0];
        else if (rb[0] - ra[0] !== delta) return BLOCK_DIFFERENT;
        if (ra[0] <= maxStayed) return BLOCK_DIFFERENT;
        if (ra[0] < minMoved) minMoved = ra[0];
      }
      i = ra[1];
      j = rb[1];
    }
    if (commit) {
      model.delta = delta;
      model.maxStayed = maxStayed;
      model.minMoved = minMoved;
    }
    return BLOCK_MOVED;
  }

  // Applies `model` to an on-screen block: every line attribute at or past the
  // edit moves by its delta. The node is the decorated one, so this also
  // covers attributes a decorator carried over (Mermaid copies the <pre>'s
  // range onto its diagram).
  function renumberSourceLines(node, model) {
    function renumber(el) {
      for (var k = 0; k < LINE_ATTRS.length; k++) {
        var v = el.getAttribute(LINE_ATTRS[k]);
        if (v === null) continue;
        var n = parseInt(v, 10);
        if (n >= model.minMoved) el.setAttribute(LINE_ATTRS[k], String(n + model.delta));
      }
    }
    renumber(node);
    var found = node.querySelectorAll(LINE_ATTR_SELECTOR);
    for (var i = 0; i < found.length; i++) renumber(found[i]);
  }

  // Put the body in the DOM and return the roots that need decorating, or null
  // when the push changed nothing. Synchronous by contract: renderContent()
  // depends on the text being on screen before it waits on anything.
  function applyBlocks(html) {
    var blocks = splitBlocks(html);

    if (!blocks || !prevBlocks.length || content.children.length !== prevBlocks.length) {
      if (blocks) {
        content.replaceChildren.apply(content, blocks);
        prevBlocks = blocks.map(function (el) { return el.outerHTML; });
      } else {
        content.innerHTML = html;
        prevBlocks = [];
      }
      return [content];
    }

    var next = blocks.map(function (el) { return el.outerHTML; });
    var oldCount = prevBlocks.length;
    var newCount = next.length;

    // An edit that adds or removes lines changes the line numbers of every
    // block below it (and of blocks above it that refer below it). Those are
    // still the same blocks: they are kept and renumbered in place rather than
    // rebuilt and laid out again. `moved` holds their old indices.
    var model = newShiftModel();
    var moved = [];
    function keep(o, n) {
      var r = matchBlock(prevBlocks[o], next[n], model, true);
      if (r === BLOCK_MOVED) moved.push(o);
      return r !== BLOCK_DIFFERENT;
    }
    function fits(o, n) { return matchBlock(prevBlocks[o], next[n], model, false) !== BLOCK_DIFFERENT; }

    // A lone differing block followed by a matching one is not the edit but a
    // block that reflects it from elsewhere, such as the [TOC] after a heading
    // changed. It is rebuilt on its own (one-for-one) and the walk goes on.
    var swaps = [];

    var head = 0;
    var limit = Math.min(oldCount, newCount);
    while (head < limit) {
      if (!keep(head, head)) {
        if (head + 1 >= limit || !fits(head + 1, head + 1)) break;
        swaps.push([head, head]);
      }
      head++;
    }

    var tail = 0;
    var tailLimit = limit - head;
    while (tail < tailLimit) {
      if (!keep(oldCount - 1 - tail, newCount - 1 - tail)) {
        if (tail + 1 >= tailLimit || !fits(oldCount - 2 - tail, newCount - 2 - tail)) break;
        swaps.push([oldCount - 1 - tail, newCount - 1 - tail]);
      }
      tail++;
    }

    // Only after both walks: the model's bounds are final now.
    for (var m = 0; m < moved.length; m++)
      renumberSourceLines(content.children[moved[m]], model);

    var removeCount = oldCount - head - tail;
    var addCount = newCount - head - tail;

    // Identical push (tab activation, a no-op edit), or one that only moved
    // line numbers (a blank line added or removed): nothing to rebuild.
    if (removeCount === 0 && addCount === 0 && !swaps.length) {
      prevBlocks = next;
      return null;
    }

    // Before the middle run is removed, while old indices are still valid.
    var roots = [];
    for (var w = 0; w < swaps.length; w++) {
      content.replaceChild(blocks[swaps[w][1]], content.children[swaps[w][0]]);
      roots.push(blocks[swaps[w][1]]);
    }

    // Resolved before any removal, so it stays valid as the insert anchor.
    var anchor = content.children[head + removeCount] || null;
    for (var r = 0; r < removeCount; r++) content.children[head].remove();

    var fragment = document.createDocumentFragment();
    for (var a = 0; a < addCount; a++) {
      roots.push(blocks[head + a]);
      fragment.appendChild(blocks[head + a]);
    }
    content.insertBefore(fragment, anchor);
    prevBlocks = next;
    return roots;
  }

  // Roots whose decoration pass has not completed. A newer push abandons the
  // running pass, yet usually keeps the very blocks it was decorating -- they
  // are unchanged, so the diff does not hand them out again. They are carried
  // into the next pass instead, and cleared only by a pass that finishes
  // while it is still the newest. Decorators skip what they already did, so
  // a block a superseded pass got halfway through is safe to revisit.
  var pendingRoots = [];

  function queueDecoration(roots) {
    var all = pendingRoots.concat(roots || []);
    var out = [];
    for (var i = 0; i < all.length; i++) {
      var r = all[i];
      if (r === content) { out = [content]; break; }
      // Replaced by a later push (or swapped for its Mermaid diagram).
      if (!r.isConnected || out.indexOf(r) >= 0) continue;
      out.push(r);
    }
    pendingRoots = out;
    return out.length ? out : null;
  }

  // What a 'patch' message is checked against. bodySeq is the seq of the
  // last push applied in full; blocksTrusted says the host's block split of
  // that body (its "blocks" count) agrees with #content's element children.
  // A patch whose base is not bodySeq, or that arrives untrusted, is refused
  // and the host answers with the whole body -- the DOM is never patched on
  // a guess.
  var bodySeq = 0;
  var blocksTrusted = false;

  function renderContent(html, seq, hostBlocks) {
    var token = ++renderToken;
    var roots;

    // The DOM goes in first, before anything is awaited. ensureFeatures() can
    // have to compile highlight.js (~1 MB), KaTeX or Mermaid (~3.5 MB) for this
    // page, and awaiting that ahead of the insertion meant a document holding a
    // single fenced block or diagram showed *no text at all* until the bundle
    // was ready. Now the prose paints immediately and the code, math and
    // diagrams upgrade in place a moment later.
    try {
      roots = applyBlocks(html);
    } catch (e) {
      // Never leave the preview stale: fall back to the whole-body path.
      console.error('[fastmd] incremental render failed, rebuilding', e);
      content.innerHTML = html;
      prevBlocks = [];
      roots = [content];
    }
    bodySeq = seq;
    blocksTrusted = typeof hostBlocks === 'number' && content.children.length === hostBlocks;
    if (typeof hostBlocks === 'number' && !blocksTrusted)
      post({ type: 'blocksMismatch', seq: seq });
    finishRender(token, roots, html, seq);
  }

  function finishRender(token, roots, featureHtml, seq) {
    roots = queueDecoration(roots);

    ensureFeatures(featureHtml).then(function () {
      if (token !== renderToken || !roots) return;
      return decorate(roots, token);
    }).catch(function (e) {
      // The text is already displayed, so a failed decorator costs styling on
      // this push, not the document -- rebuilding it here would throw away a
      // correct body to no purpose.
      console.error('[fastmd] decoration failed', e);
    }).then(function () {
      if (token !== renderToken) return;
      pendingRoots = [];
      reportRendered(seq);
    });
  }

  // A patch carries only what changed since the push named by `base`. The
  // host diffed the two bodies with the same rules as applyBlocks(), so:
  //   shift [T, delta]  every line attribute >= T in a moved block moves by delta
  //   moved [[a, b]..]  old indices of the blocks kept but renumbered
  //   ops [[i, n, k, html]..]  replace n old blocks at i with html's k blocks,
  //                    ascending by i and disjoint from each other and `moved`
  // Everything not named stays exactly as it is, decoration included.
  function refusePatch(msg, why) {
    console.warn('[fastmd] preview patch refused (' + why + '), requesting the whole body');
    blocksTrusted = false;
    post({ type: 'resync', seq: msg.seq });
  }

  function applyPatch(msg) {
    var ops = msg.ops || [];
    if (!blocksTrusted || msg.base !== bodySeq) return refusePatch(msg, 'base');
    if (content.children.length !== msg.oldCount) return refusePatch(msg, 'count');

    // Parse every fragment before touching the page, so a fragment the
    // browser reads differently from the host refuses the patch cleanly.
    var parsed = [];
    var featureHtml = '';
    for (var k = 0; k < ops.length; k++) {
      var frag = ops[k][3] ? splitBlocks(ops[k][3]) : [];
      if (!frag || frag.length !== ops[k][2]) return refusePatch(msg, 'fragment');
      parsed.push(frag);
      featureHtml += ops[k][3];
    }

    var token = ++renderToken;
    var roots = [];
    try {
      if (msg.moved && msg.shift) {
        var model = { minMoved: msg.shift[0], delta: msg.shift[1] };
        for (var r = 0; r < msg.moved.length; r++)
          for (var o = msg.moved[r][0]; o <= msg.moved[r][1]; o++)
            renumberSourceLines(content.children[o], model);
      }
      // Last op first, so the indices of the ones before it stay valid.
      for (var j = ops.length - 1; j >= 0; j--) {
        var at = ops[j][0], removeCount = ops[j][1];
        var anchor = content.children[at + removeCount] || null;
        for (var x = 0; x < removeCount; x++) content.children[at].remove();
        var fragment = document.createDocumentFragment();
        for (var y = 0; y < parsed[j].length; y++) {
          roots.push(parsed[j][y]);
          fragment.appendChild(parsed[j][y]);
        }
        content.insertBefore(fragment, anchor);
      }
      if (content.children.length !== msg.blocks) throw new Error('block count after patch');
    } catch (e) {
      console.error('[fastmd] preview patch failed', e);
      prevBlocks = [];
      return refusePatch(msg, 'apply');
    }
    // The per-block HTML of a patched page is not known here, so a later
    // whole-body push rebuilds rather than diffs. Only a resync does that.
    prevBlocks = [];
    bodySeq = msg.seq;
    finishRender(token, roots, featureHtml, msg.seq);
  }

  function scrollFraction() {
    var max = document.documentElement.scrollHeight - window.innerHeight;
    return max > 0 ? (window.scrollY / max) : 0;
  }

  function scrollToFraction(f, smooth) {
    var max = document.documentElement.scrollHeight - window.innerHeight;
    suppressScroll = true;
    var top = Math.max(0, Math.round(f * max));
    if (smooth && !reducedMotion())
      window.scrollTo({ top: top, behavior: 'smooth' });
    else
      window.scrollTo(0, top);
    if (smooth) {
      window.setTimeout(function () {
        post({ type: 'scrollRestoreFinished' });
      }, 500);
    }
    // release on next frame so we don't echo our own programmatic scroll back
    requestAnimationFrame(function () { suppressScroll = false; });
  }

  // One-time Editor -> Preview sync after session restore (see
  // MainWindow::restoreTabState). Finds the mapped block whose
  // data-src-start/data-src-end range contains `line`, or else the nearest
  // one, and centers it vertically. Prefer the narrowest containing range so
  // nested paragraphs/list items win over their enclosing list or blockquote.
  function depthOf(el) {
    var depth = 0;
    for (var parent = el.parentElement; parent; parent = parent.parentElement) depth++;
    return depth;
  }

  function scrollToSourceLine(line, flash) {
    var nodes = content.querySelectorAll('[data-src-start]');
    if (!nodes.length) return;
    var best = null, bestDist = Infinity, bestSpan = Infinity, bestDepth = -1;
    var bestContains = false;
    for (var i = 0; i < nodes.length; i++) {
      var s = parseInt(nodes[i].getAttribute('data-src-start'), 10);
      if (isNaN(s)) continue;
      var eAttr = nodes[i].getAttribute('data-src-end');
      var e = eAttr ? parseInt(eAttr, 10) : s;
      if (isNaN(e)) e = s;
      var span = Math.max(0, e - s);
      if (line >= s && line <= e) {
        // Depth is only needed to break a tie between two equally narrow
        // containing ranges, so it is measured for the two candidates involved
        // instead of walking every node's ancestor chain to the root.
        if (!bestContains || span < bestSpan) {
          best = nodes[i];
          bestDist = 0;
          bestSpan = span;
          bestDepth = -1;
          bestContains = true;
        } else if (span === bestSpan) {
          if (bestDepth < 0) bestDepth = depthOf(best);
          var depth = depthOf(nodes[i]);
          if (depth > bestDepth) {
            best = nodes[i];
            bestDist = 0;
            bestDepth = depth;
          }
        }
        continue;
      }
      var dist = line < s ? (s - line) : (line - e);
      if (!bestContains && (dist < bestDist || (dist === bestDist && span < bestSpan))) {
        bestDist = dist;
        bestSpan = span;
        bestDepth = -1;
        best = nodes[i];
      }
    }
    if (!best) return;

    if (flash) {
      best.classList.remove('source-nav-flash');
      void best.offsetWidth;
      best.classList.add('source-nav-flash');
    }

    var rect = best.getBoundingClientRect();
    var max = document.documentElement.scrollHeight - window.innerHeight;
    var target = window.scrollY + rect.top + rect.height / 2 - window.innerHeight / 2;
    target = Math.max(0, Math.min(Math.max(0, max), Math.round(target)));
    suppressScroll = true;
    window.scrollTo({ top: target, behavior: reducedMotion() ? 'auto' : 'smooth' });
    window.setTimeout(function () { suppressScroll = false; }, reducedMotion() ? 0 : 700);
  }

  // Footnote navigation stays inside the already-rendered DOM. The animation
  // uses one layout measurement and requestAnimationFrame, rather than asking
  // the browser to guess a duration for scrollIntoView().
  var footnoteScrollFrame = 0;
  function reducedMotion() {
    return window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  }

  function flashFootnoteReference(el) {
    if (!el) return;
    el.classList.remove('footnote-ref-flash');
    void el.offsetWidth;
    el.classList.add('footnote-ref-flash');
  }

  function scrollToFootnoteElement(el, afterScroll) {
    if (!el) return;
    if (footnoteScrollFrame) cancelAnimationFrame(footnoteScrollFrame);
    var start = window.scrollY;
    var max = Math.max(0, document.documentElement.scrollHeight - window.innerHeight);
    var target = Math.max(0, Math.min(max,
      start + el.getBoundingClientRect().top - 24));
    if (reducedMotion() || Math.abs(target - start) < 1) {
      suppressScroll = true;
      window.scrollTo(0, target);
      requestAnimationFrame(function () {
        suppressScroll = false;
        if (afterScroll) afterScroll();
      });
      return;
    }
    var started = performance.now();
    var duration = 300;
    suppressScroll = true;
    function step(now) {
      var progress = Math.min(1, (now - started) / duration);
      var eased = 1 - Math.pow(1 - progress, 3);
      window.scrollTo(0, Math.round(start + (target - start) * eased));
      if (progress < 1) {
        footnoteScrollFrame = requestAnimationFrame(step);
      } else {
        footnoteScrollFrame = 0;
        suppressScroll = false;
        if (afterScroll) afterScroll();
      }
    }
    footnoteScrollFrame = requestAnimationFrame(step);
  }

  var suppressScroll = false;
  var scrollQueued = false;
  window.addEventListener('scroll', function () {
    if (suppressScroll) return;
    if (scrollQueued) return;
    scrollQueued = true;
    requestAnimationFrame(function () {
      scrollQueued = false;
      post({ type: 'scroll', fraction: scrollFraction() });
    });
  }, { passive: true });

  // Ctrl+wheel zoom. WebView2 is a native child window Qt never sees wheel
  // events for, and its own built-in Ctrl+wheel page zoom is turned off on the
  // host side (IsZoomControlEnabled=false) so it doesn't fight our font-size
  // based zoom -- forward it through the same message channel as scroll sync
  // instead, so MainWindow's existing zoomIn()/zoomOut() stay the single
  // source of truth for preview zoom.
  window.addEventListener('wheel', function (ev) {
    if (!ev.ctrlKey) return;
    ev.preventDefault();
    post({ type: 'zoom', delta: ev.deltaY < 0 ? 1 : -1 });
  }, { passive: false });

  // Intercept link activation: everything opens in the default browser. This is
  // belt-and-suspenders with the host's NavigationStarting guard.
  document.addEventListener('click', function (ev) {
    var a = ev.target && ev.target.closest ? ev.target.closest('a') : null;
    if (!a) return;
    if (!hostBridge) return;
    var href = a.getAttribute('href') || '';
    ev.preventDefault();
    if (!href) return;
    if (href.charAt(0) === '#') {
      var el = document.getElementById(href.slice(1)) ||
               document.querySelector('[name="' + href.slice(1) + '"]');
      var isFootnoteTarget = el && el.id && el.id.indexOf('fn-') === 0;
      var isFootnoteBacklink = el && el.id && el.id.indexOf('fnref-') === 0;
      if (isFootnoteTarget || isFootnoteBacklink) {
        scrollToFootnoteElement(el, isFootnoteBacklink ? function () {
          flashFootnoteReference(el);
        } : null);
      } else if (el) {
        el.scrollIntoView({ behavior: 'auto', block: 'start' });
      }
      // WebView2 is a native child HWND, so an anchor click leaves real OS
      // keyboard focus in the preview even though the editor remains the
      // active Qt widget. Return focus explicitly after internal navigation.
      post({ type: 'focusEditor' });
      return;
    }
    post({ type: 'openExternal', url: a.href || href });
  }, true);

  // One reusable, rendered-DOM footnote popover. It is deliberately outside
  // #content so it can float above the document without affecting layout.
  var footnotePopover = null;
  var activeFootnoteRef = null;
  var footnoteHideTimer = 0;

  function ensureFootnotePopover() {
    if (footnotePopover) return footnotePopover;
    footnotePopover = document.createElement('div');
    footnotePopover.id = 'footnote-popover';
    footnotePopover.className = 'footnote-popover';
    footnotePopover.setAttribute('role', 'tooltip');
    footnotePopover.setAttribute('aria-hidden', 'true');
    document.body.appendChild(footnotePopover);
    footnotePopover.addEventListener('mouseenter', function () {
      if (footnoteHideTimer) { clearTimeout(footnoteHideTimer); footnoteHideTimer = 0; }
    });
    footnotePopover.addEventListener('mouseleave', scheduleFootnoteHide);
    return footnotePopover;
  }

  function footnoteReference(node) {
    var a = node && node.closest ? node.closest('a') : null;
    if (!a || !a.parentElement || !a.getAttribute('href') ||
        a.closest('#footnote-popover')) return null;
    var href = a.getAttribute('href');
    if (href.charAt(0) !== '#' || href.slice(1).indexOf('fn-') !== 0) return null;
    var target = document.getElementById(href.slice(1));
    return target && target.tagName.toLowerCase() === 'li' ? a : null;
  }

  function positionFootnotePopover(x, y) {
    if (!footnotePopover) return;
    var margin = 12;
    var width = footnotePopover.offsetWidth;
    var height = footnotePopover.offsetHeight;
    var left = Math.max(margin, Math.min(window.innerWidth - width - margin, x + 14));
    var top = y + 18;
    if (top + height > window.innerHeight - margin)
      top = Math.max(margin, y - height - 14);
    footnotePopover.style.left = Math.round(left) + 'px';
    footnotePopover.style.top = Math.round(top) + 'px';
  }

  function showFootnotePopover(ref, x, y) {
    var href = ref.getAttribute('href');
    var source = document.getElementById(href.slice(1));
    if (!source) return;
    if (footnoteHideTimer) { clearTimeout(footnoteHideTimer); footnoteHideTimer = 0; }
    var popup = ensureFootnotePopover();
    if (activeFootnoteRef !== ref) {
      var clone = source.cloneNode(true);
      clone.querySelectorAll('.footnote-backlinks').forEach(function (node) { node.remove(); });
      popup.innerHTML = clone.innerHTML;
      if (activeFootnoteRef) activeFootnoteRef.removeAttribute('aria-describedby');
      activeFootnoteRef = ref;
      ref.setAttribute('aria-describedby', 'footnote-popover');
    }
    popup.setAttribute('aria-hidden', 'false');
    popup.classList.add('is-visible');
    positionFootnotePopover(x, y);
  }

  function scheduleFootnoteHide() {
    if (footnoteHideTimer) clearTimeout(footnoteHideTimer);
    footnoteHideTimer = setTimeout(function () {
      footnoteHideTimer = 0;
      if (footnotePopover) {
        footnotePopover.classList.remove('is-visible');
        footnotePopover.setAttribute('aria-hidden', 'true');
      }
      if (activeFootnoteRef) activeFootnoteRef.removeAttribute('aria-describedby');
      activeFootnoteRef = null;
    }, 140);
  }

  content.addEventListener('mouseover', function (ev) {
    var ref = footnoteReference(ev.target);
    if (!ref) return;
    showFootnotePopover(ref, ev.clientX, ev.clientY);
  });
  content.addEventListener('mousemove', function (ev) {
    var ref = footnoteReference(ev.target);
    if (ref && ref === activeFootnoteRef) positionFootnotePopover(ev.clientX, ev.clientY);
  });
  content.addEventListener('mouseout', function (ev) {
    var ref = footnoteReference(ev.target);
    if (ref && (!ev.relatedTarget || !ref.contains(ev.relatedTarget))) scheduleFootnoteHide();
  });
  content.addEventListener('focusin', function (ev) {
    var ref = footnoteReference(ev.target);
    if (ref) {
      var rect = ref.getBoundingClientRect();
      showFootnotePopover(ref, rect.left, rect.bottom);
    }
  });
  content.addEventListener('focusout', function (ev) {
    var ref = footnoteReference(ev.target);
    if (ref && (!ev.relatedTarget || !ref.contains(ev.relatedTarget))) scheduleFootnoteHide();
  });
  document.addEventListener('keydown', function (ev) {
    if (ev.key === 'Escape' && footnotePopover && footnotePopover.classList.contains('is-visible')) {
      ev.preventDefault();
      scheduleFootnoteHide();
    }
  });

  // Link destinations use the native status bar rather than a second floating
  // tooltip. Footnote references already own the rich popover above, and
  // backlinks already have descriptive titles, so leave both alone.
  var hoveredLink = null;

  function previewLinkForHover(node) {
    var a = node && node.closest ? node.closest('a') : null;
    if (!a || a.closest('#footnote-popover') || footnoteReference(a) ||
        a.classList.contains('footnote-backref')) return null;
    return (a.href || a.getAttribute('href')) ? a : null;
  }

  function clearHoveredLink() {
    if (!hoveredLink) return;
    hoveredLink = null;
    post({ type: 'linkHover', url: '' });
  }

  content.addEventListener('mouseover', function (ev) {
    var a = previewLinkForHover(ev.target);
    if (!a || a === hoveredLink) return;
    hoveredLink = a;
    post({ type: 'linkHover', url: a.href || a.getAttribute('href') || '' });
  });
  content.addEventListener('mouseout', function (ev) {
    if (!hoveredLink || (ev.relatedTarget && hoveredLink.contains(ev.relatedTarget))) return;
    if (!previewLinkForHover(ev.relatedTarget)) clearHoveredLink();
  });
  window.addEventListener('blur', clearHoveredLink);
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) clearHoveredLink();
  });

  // -------------------------------------------------------------------------
  // Preview double-click source highlighting. Every block-level tag md4c
  // emits for the live preview (see MarkdownPreviewRenderer) carries
  // data-src-start / data-src-end (1-based source line). Double-click resolves
  // the mapped line and asks the host to flash it in the Editor and focus it
  // (without touching caret, selection, or scroll position), and never
  // interferes with the browser's native double-click text selection.
  // -------------------------------------------------------------------------

  // Elements whose own double-click already means something else, or that
  // have no meaningful "source" of their own -- clicks landing on them never
  // resolve to a highlight target.
  var NAV_EXCLUDE_SEL = 'a, .task-list-marker, input[type="checkbox"], ' +
    '.mermaid-diagram, #mmd-modal, #outline-btn, img';

  function resolveSourceEl(x, y) {
    var el = document.elementFromPoint(x, y);
    if (!el) return null;
    if (el.closest(NAV_EXCLUDE_SEL)) return null;
    return el.closest('[data-src-line], [data-src-start]');
  }

  function sourceLineInHighlightedCode(x, y) {
    var el = document.elementFromPoint(x, y);
    var code = el && el.closest ? el.closest('pre[data-src-start] > code[class*="language-"]') : null;
    if (!code) return 0;
    var pre = code.parentElement;
    var start = parseInt(pre.getAttribute('data-src-start'), 10);
    if (!start) return 0;
    var caret = document.caretRangeFromPoint ? document.caretRangeFromPoint(x, y) : null;
    if (!caret || !code.contains(caret.startContainer)) return start;
    try {
      var before = document.createRange();
      before.selectNodeContents(code);
      before.setEnd(caret.startContainer, caret.startOffset);
      // The mapped <pre> starts at the first code-content line. highlight.js
      // may replace every inline span, so derive the clicked physical line from
      // the live text while retaining that exact source origin.
      return start + (before.toString().match(/\n/g) || []).length;
    } catch (e) { return start; }
  }

  content.addEventListener('dblclick', function (ev) {
    var codeLine = sourceLineInHighlightedCode(ev.clientX, ev.clientY);
    if (codeLine) {
      post({ type: 'previewFlash', line: codeLine });
      return;
    }
    var m = resolveSourceEl(ev.clientX, ev.clientY);
    if (!m) return;
    var line = parseInt(m.getAttribute('data-src-line') ||
                        m.getAttribute('data-src-start'), 10);
    if (!line) return;
    post({ type: 'previewFlash', line: line });
  });

  // -------------------------------------------------------------------------
  // Custom right-click context menu. The browser's own context menu is
  // disabled host-side (AreDefaultContextMenusEnabled=false); the host builds
  // a native Qt menu instead and positions it at (x, y), which is why the
  // event's client coordinates -- not a resolved element -- are all that's
  // reported here. Mapped content also carries the source line resolved by
  // the same helper used by double-click navigation. Mermaid diagrams also carry their original SVG and a PNG
  // rasterization for the native diagram menu.
  // -------------------------------------------------------------------------
  function mermaidPngDataUrl(svgElement) {
    return new Promise(function (resolve) {
      var size = svgNaturalSize(svgElement);
      var rasterSvg = svgElement.cloneNode(true);
      rasterSvg.setAttribute('width', Math.ceil(size.w));
      rasterSvg.setAttribute('height', Math.ceil(size.h));
      var svgText = new XMLSerializer().serializeToString(rasterSvg);
      var image = new Image();
      image.onload = function () {
        try {
          var canvas = document.createElement('canvas');
          canvas.width = Math.max(1, Math.ceil(size.w));
          canvas.height = Math.max(1, Math.ceil(size.h));
          canvas.getContext('2d').drawImage(image, 0, 0, canvas.width, canvas.height);
          resolve(canvas.toDataURL('image/png'));
        } catch (e) {
          resolve('');
        }
      };
      image.onerror = function () { resolve(''); };
      image.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svgText);
    });
  }

  var standaloneMermaidMenu = null;

  function downloadDataUrl(dataUrl, name) {
    var link = document.createElement('a');
    link.href = dataUrl;
    link.download = name;
    document.body.appendChild(link);
    link.click();
    link.remove();
  }

  function showStandaloneMermaidMenu(svg, x, y) {
    if (!standaloneMermaidMenu) {
      standaloneMermaidMenu = document.createElement('div');
      standaloneMermaidMenu.className = 'fastmd-mermaid-save-menu';
      document.body.appendChild(standaloneMermaidMenu);
      document.addEventListener('mousedown', function (ev) {
        if (!standaloneMermaidMenu.contains(ev.target))
          standaloneMermaidMenu.classList.remove('open');
      });
    }
    standaloneMermaidMenu.replaceChildren();
    var svgButton = document.createElement('button');
    svgButton.type = 'button';
    svgButton.textContent = 'Save SVG';
    svgButton.addEventListener('click', function () {
      standaloneMermaidMenu.classList.remove('open');
      downloadDataUrl('data:image/svg+xml;charset=utf-8,'
        + encodeURIComponent(new XMLSerializer().serializeToString(svg)), 'mermaid-diagram.svg');
    });
    var pngButton = document.createElement('button');
    pngButton.type = 'button';
    pngButton.textContent = 'Save PNG';
    pngButton.addEventListener('click', function () {
      standaloneMermaidMenu.classList.remove('open');
      mermaidPngDataUrl(svg).then(function (dataUrl) {
        if (dataUrl) downloadDataUrl(dataUrl, 'mermaid-diagram.png');
      });
    });
    standaloneMermaidMenu.appendChild(svgButton);
    standaloneMermaidMenu.appendChild(pngButton);
    standaloneMermaidMenu.style.left = x + 'px';
    standaloneMermaidMenu.style.top = y + 'px';
    standaloneMermaidMenu.classList.add('open');
  }

  document.addEventListener('contextmenu', function (ev) {
    if (!hostBridge) {
      var standaloneSvg = ev.target && ev.target.closest
        ? ev.target.closest('.mermaid-diagram svg') : null;
      if (!standaloneSvg) return;
      ev.preventDefault();
      showStandaloneMermaidMenu(standaloneSvg, ev.clientX, ev.clientY);
      return;
    }
    ev.preventDefault();
    var sel = window.getSelection();
    var hasSelection = !!(sel && sel.toString().length > 0);
    var diagram = ev.target && ev.target.closest ? ev.target.closest('.mermaid-diagram') : null;
    var mapped = resolveSourceEl(ev.clientX, ev.clientY);
    // Text-run metadata is more specific than its enclosing block. The old
    // context-menu path looked only for data-src-start, so right-clicking the
    // normal data-src-line spans always sent 0 and disabled Go to Source.
    var sourceLine = sourceLineInHighlightedCode(ev.clientX, ev.clientY);
    if (!sourceLine && mapped) {
      sourceLine = parseInt(mapped.getAttribute('data-src-line') ||
                            mapped.getAttribute('data-src-start'), 10);
    }
    if (!sourceLine) sourceLine = 0;
    var svg = diagram ? diagram.querySelector('svg') : null;
    if (!svg) {
      post({ type: 'contextMenu', x: ev.clientX, y: ev.clientY, hasSelection: hasSelection,
        sourceLine: sourceLine });
      return;
    }
    var mermaidSvg = svg.outerHTML;
    mermaidPngDataUrl(svg).then(function (pngDataUrl) {
      post({ type: 'contextMenu', x: ev.clientX, y: ev.clientY,
        hasSelection: hasSelection, sourceLine: 0, mermaidSvg: mermaidSvg,
        mermaidPngDataUrl: pngDataUrl });
    });
  });

  // One delegated handler keeps task interaction cheap even for large lists.
  // The source range is emitted by the mapped renderer, never inferred from
  // the visible task label, so repeated labels and nested lists are safe.
  content.addEventListener('click', function (ev) {
    var box = ev.target && ev.target.closest ? ev.target.closest('input[type="checkbox"]') : null;
    if (!box || !content.contains(box)) return;
    if (!hostBridge) return;
    var item = box.closest('[data-fastmd-task-start]');
    if (!item) return;
    ev.preventDefault();
    ev.stopImmediatePropagation();
    var current = box.getAttribute('data-fastmd-checked') === '1';
    post({ type: 'taskToggle', start: parseInt(item.getAttribute('data-fastmd-task-start'), 10),
           end: parseInt(item.getAttribute('data-fastmd-task-end'), 10), checked: !current });
    post({ type: 'focusEditor' });
  });

  // -------------------------------------------------------------------------
  // Outline button — hover-expands to reveal its label; forced expanded while
  // the (natively hosted) outline popup is open. Purely CSS/JS driven so the
  // animation is composited by the browser, not a native Qt window resize.
  // -------------------------------------------------------------------------
  var outlineBtn = document.getElementById('outline-btn');
  var outlinePopupOpen = false;

  function setOutlineExpanded(expanded) {
    if (outlineBtn) outlineBtn.classList.toggle('expanded', expanded);
  }

  if (outlineBtn) {
    outlineBtn.addEventListener('mouseenter', function () { setOutlineExpanded(true); });
    outlineBtn.addEventListener('mouseleave', function () {
      if (!outlinePopupOpen) setOutlineExpanded(false);
    });
    outlineBtn.addEventListener('click', function () { post({ type: 'outlineClicked' }); });
  }

  // -------------------------------------------------------------------------
  // Diagram viewer — click a rendered Mermaid diagram to open a lightweight
  // modal with wheel zoom, drag-to-pan, Fit to Window, Reset (100%), and Esc to
  // close. Created lazily on first use; inherits the current app theme.
  // -------------------------------------------------------------------------
  var modal = null, modalStage = null, modalCanvas = null;
  var mScale = 1, mTx = 0, mTy = 0, mBaseFit = 1;
  var mPanning = false, mPanX = 0, mPanY = 0;

  function makeDiagramInteractive(container) {
    container.classList.add('mermaid-clickable');
    container.title = 'Click to open in viewer';
  }

  function buildModal() {
    modal = document.createElement('div');
    modal.id = 'mmd-modal';
    modal.setAttribute('data-theme', mermaidDark ? 'dark' : 'light');
    modal.innerHTML =
      '<div class="mmd-modal-bar">' +
      '  <button data-act="fit"   type="button">Fit to Window</button>' +
      '  <button data-act="reset" type="button">Reset (100%)</button>' +
      '  <span class="mmd-modal-zoom"></span>' +
      '  <button data-act="close" class="mmd-modal-close" type="button" title="Close (Esc)">&#x2715;</button>' +
      '</div>' +
      '<div class="mmd-modal-stage"><div class="mmd-modal-canvas"></div></div>';
    document.body.appendChild(modal);

    modalStage = modal.querySelector('.mmd-modal-stage');
    modalCanvas = modal.querySelector('.mmd-modal-canvas');

    modal.querySelector('[data-act="fit"]').addEventListener('click', fitToWindow);
    modal.querySelector('[data-act="reset"]').addEventListener('click', resetZoom);
    modal.querySelector('[data-act="close"]').addEventListener('click', closeModal);
    // Click the dim backdrop (but not the diagram/toolbar) to dismiss.
    modalStage.addEventListener('mousedown', function (ev) {
      if (ev.target === modalStage) { closeModal(); }
    });

    modalStage.addEventListener('wheel', function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      var factor = ev.deltaY < 0 ? 1.1 : 1 / 1.1;
      zoomAt(ev.clientX, ev.clientY, factor);
    }, { passive: false });

    modalStage.addEventListener('mousedown', function (ev) {
      if (ev.target === modalStage) return;           // backdrop handled above
      mPanning = true;
      mPanX = ev.clientX; mPanY = ev.clientY;
      modal.classList.add('mmd-panning');
      ev.preventDefault();
    });
    window.addEventListener('mousemove', function (ev) {
      if (!mPanning) return;
      mTx += ev.clientX - mPanX;
      mTy += ev.clientY - mPanY;
      mPanX = ev.clientX; mPanY = ev.clientY;
      applyTransform();
    });
    window.addEventListener('mouseup', function () {
      if (!mPanning) return;
      mPanning = false;
      modal.classList.remove('mmd-panning');
    });
  }

  function openModal(svgHtml) {
    if (!modal) buildModal();
    syncModalTheme();
    modalCanvas.innerHTML = svgHtml;
    var svg = modalCanvas.querySelector('svg');
    if (svg) {
      // Give the SVG a concrete natural size (from its viewBox) so the canvas
      // has real dimensions for our transform to scale — Mermaid's own
      // max-width/percentage sizing would otherwise collapse it in the stage.
      var sz = svgNaturalSize(svg);
      svg.removeAttribute('width');
      svg.removeAttribute('height');
      svg.style.width = sz.w + 'px';
      svg.style.height = sz.h + 'px';
      svg.style.maxWidth = 'none';
      svg.style.maxHeight = 'none';
    }
    modal.classList.add('open');
    // Fit once layout is known.
    requestAnimationFrame(fitToWindow);
  }

  function closeModal() {
    if (modal) modal.classList.remove('open');
    mPanning = false;
  }

  function syncModalTheme() {
    if (modal) modal.setAttribute('data-theme', mermaidDark ? 'dark' : 'light');
  }

  function svgNaturalSize(svg) {
    var w = 0, h = 0;
    if (svg) {
      var vb = svg.viewBox && svg.viewBox.baseVal;
      if (vb && vb.width && vb.height) { w = vb.width; h = vb.height; }
      if (!w || !h) {
        try { var b = svg.getBBox(); w = w || b.width; h = h || b.height; } catch (e) {}
      }
    }
    return { w: w || 100, h: h || 100 };
  }

  function fitToWindow() {
    var svg = modalCanvas ? modalCanvas.querySelector('svg') : null;
    if (!svg) return;
    var sz = svgNaturalSize(svg);
    var pad = 48;
    var availW = modalStage.clientWidth - pad;
    var availH = modalStage.clientHeight - pad;
    mBaseFit = Math.min(availW / sz.w, availH / sz.h);
    if (!isFinite(mBaseFit) || mBaseFit <= 0) mBaseFit = 1;
    mScale = mBaseFit;
    mTx = 0; mTy = 0;
    applyTransform();
  }

  function resetZoom() {
    mScale = 1; mTx = 0; mTy = 0;
    applyTransform();
  }

  function zoomAt(clientX, clientY, factor) {
    var rect = modalStage.getBoundingClientRect();
    // Cursor position relative to the stage center (the transform origin).
    var cx = clientX - rect.left - rect.width / 2;
    var cy = clientY - rect.top - rect.height / 2;
    var newScale = Math.max(0.05, Math.min(40, mScale * factor));
    var ratio = newScale / mScale;
    // Keep the point under the cursor stationary while scaling.
    mTx = cx - (cx - mTx) * ratio;
    mTy = cy - (cy - mTy) * ratio;
    mScale = newScale;
    applyTransform();
  }

  function applyTransform() {
    if (!modalCanvas) return;
    modalCanvas.style.transform =
      'translate(' + mTx + 'px,' + mTy + 'px) scale(' + mScale + ')';
    var label = modal.querySelector('.mmd-modal-zoom');
    if (label) label.textContent = Math.round(mScale * 100) + '%';
  }

  // Open the viewer when a rendered diagram is clicked.
  content.addEventListener('click', function (ev) {
    var d = ev.target && ev.target.closest ? ev.target.closest('.mermaid-diagram') : null;
    if (!d) return;
    var svg = d.querySelector('svg');
    if (!svg) return;
    openModal(d.innerHTML);
  });

  // Esc closes the viewer. (WebView2 leaves Escape for the page — it's not in
  // the host accelerator map.)
  window.addEventListener('keydown', function (ev) {
    if (ev.key === 'Escape' && modal && modal.classList.contains('open')) {
      ev.preventDefault();
      closeModal();
    }
  });

  // Host -> page messages.
  if (hostBridge) {
    host.addEventListener('message', function (ev) {
      var msg = ev.data;
      if (!msg || !msg.type) return;
      switch (msg.type) {
        case 'content':          renderContent(msg.html || '', msg.seq, msg.blocks); break;
        case 'patch':            applyPatch(msg); break;
        case 'theme':            setTheme(!!msg.dark, msg.fs); break;
        case 'font':             setBodyFont(msg.family || '', msg.faces || ''); break;
        case 'spacing':          setDocumentSpacing(msg.lh || '', msg.pspace || '', msg.ligap || ''); break;
        case 'scrollToFraction': scrollToFraction(msg.fraction || 0, !!msg.smooth); break;
        case 'scrollToSourceLine': scrollToSourceLine(msg.line || 0, !!msg.flash); break;
        case 'scrollToAnchor':
          var t = document.getElementById(msg.id) ||
                  document.querySelector('[name="' + msg.id + '"]');
          if (t) t.scrollIntoView({ behavior: 'auto', block: 'start' });
          break;
        case 'outlinePopupOpen':
          outlinePopupOpen = !!msg.open;
          setOutlineExpanded(outlinePopupOpen);
          break;
      }
    });
  }

  // A standalone HTML export embeds this same shell and its assets. With no
  // WebView2 host, render the already-present document body once instead of
  // waiting for a bridge message.
  if (hostBridge) post({ type: 'ready' });
  else renderContent(content.innerHTML, 0);
})();
