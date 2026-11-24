// Line-shifting edits: blocks after an edit that adds or removes source lines
// must keep their DOM nodes, with their data-src-* numbers moved in place, and
// the result must match a from-scratch render of the new body exactly
// (source-line attributes included).
//
// Synthetic bodies always run. Real bodies from shiftcases.py (shiftdata.js)
// run when present -- see README.
(function () {
  var results = [];
  var failures = 0;
  var el = document.getElementById('content');

  function pusherFor(win) {
    var waiters = {}, seq = 0;
    var posted = win.__posted;
    var origPush = posted.push.bind(posted);
    posted.push = function (m) {
      origPush(m);
      if (m && m.type === 'rendered' && waiters[m.seq]) {
        var w = waiters[m.seq]; delete waiters[m.seq]; w();
      }
      return posted.length;
    };
    return function (html) {
      var want = ++seq;
      return new Promise(function (resolve) {
        waiters[want] = resolve;
        win.__send({ type: 'content', html: html, seq: want });
      });
    };
  }
  var push = pusherFor(window);

  // Same normalization as realsamples.js. data-src-* is deliberately kept.
  function canon(s) {
    return s
      .replace(/<svg[\s\S]*?<\/svg>/gi, '<svg/>')
      .replace(/fastmd-mmd-\d+/g, 'MMD')
      .replace(/\bid="[^"]*(mermaid|katex|mmd)[^"]*"/gi, 'id="X"')
      .replace(/\bfor="[^"]*(mermaid|katex|mmd)[^"]*"/gi, 'for="X"')
      .replace(/aria-owns="[^"]*"/g, '')
      .replace(/\s+/g, ' ')
      .trim();
  }

  function reference(html) {
    return new Promise(function (resolve) {
      var f = document.createElement('iframe');
      f.style.cssText = 'position:absolute;left:-9999px;width:900px;height:600px';
      f.onload = function () {
        var w = f.contentWindow;
        pusherFor(w)(html).then(function () {
          var dom = w.document.getElementById('content').innerHTML;
          f.remove();
          resolve(dom);
        });
      };
      f.src = 'shifttest.html?ref=1';
      document.body.appendChild(f);
    });
  }

  var hljsCalls = 0;
  function instrument() {
    if (window.hljs && !window.hljs.__wrapped) {
      var oh = window.hljs.highlightElement.bind(window.hljs);
      window.hljs.highlightElement = function (e) { hljsCalls++; return oh(e); };
      window.hljs.__wrapped = true;
    }
  }

  // Independent oracle (regexes, not the shell's scanner) for which blocks must
  // keep their nodes: the prefix and suffix whose blocks are equal with their
  // line numbers removed and whose numbers fit one edit (below).
  // Either walk steps over one differing block when the next pair matches.
  // Line numbers are attribute values inside start tags, never text.
  var TAG_RE = /<[a-zA-Z][^<>]*>/g;
  var NUM_RE = /\sdata-(?:src-start|src-end|src-line|fastmd-task-start|fastmd-task-end)="(\d+)"/g;
  function serialize(html) {
    var tpl = document.createElement('template');
    tpl.innerHTML = html;
    var out = [];
    for (var n = tpl.content.firstChild; n; n = n.nextSibling)
      if (n.nodeType === 1) out.push(n.outerHTML);
    return out;
  }
  function nums(s) {
    var out = [];
    (s.match(TAG_RE) || []).forEach(function (tag) {
      var m;
      NUM_RE.lastIndex = 0;
      while ((m = NUM_RE.exec(tag))) out.push(+m[1]);
    });
    return out;
  }
  function stripNums(s) {
    return s.replace(TAG_RE, function (tag) { return tag.replace(NUM_RE, ''); });
  }
  // The single-edit model, as intervals: numbers that stayed are below the
  // edit line T, numbers that moved are at or above it and moved by `delta`.
  // `fit` checks a pair of blocks against it and, when `commit`, narrows it.
  function oracle(base, next) {
    var a = serialize(base), b = serialize(next);
    var model = { delta: null, stayedMax: -1, movedMin: Infinity };
    function fit(x, y, commit) {
      if (x === y) return true;
      if (stripNums(x) !== stripNums(y)) return false;
      var nx = nums(x), ny = nums(y);
      var d = model.delta, lo = model.stayedMax, hi = model.movedMin;
      for (var k = 0; k < nx.length; k++) {
        if (nx[k] === ny[k]) { lo = Math.max(lo, nx[k]); continue; }
        if (d === null) d = ny[k] - nx[k];
        else if (ny[k] - nx[k] !== d) return false;
        hi = Math.min(hi, nx[k]);
      }
      if (lo >= hi) return false;
      if (commit) { model.delta = d; model.stayedMax = lo; model.movedMin = hi; }
      return true;
    }
    var limit = Math.min(a.length, b.length);
    var kept = [];   // [oldIndex, newIndex]
    var head = 0;
    while (head < limit) {
      if (fit(a[head], b[head], true)) kept.push([head, head]);
      else if (!(head + 1 < limit && fit(a[head + 1], b[head + 1], false))) break;
      head++;
    }
    var tail = 0;
    while (tail < limit - head) {
      var o = a.length - 1 - tail, n = b.length - 1 - tail;
      if (fit(a[o], b[n], true)) kept.push([o, n]);
      else if (!(tail + 1 < limit - head && fit(a[o - 1], b[n - 1], false))) break;
      tail++;
    }
    return { kept: kept, delta: model.delta || 0, oldCount: a.length, newCount: b.length };
  }

  var LINE_SELECTOR = '[data-src-start],[data-src-end],[data-src-line],' +
                      '[data-fastmd-task-start],[data-fastmd-task-end]';
  function lineSignature(node) {
    var parts = [];
    [node].concat(Array.prototype.slice.call(node.querySelectorAll(LINE_SELECTOR))).forEach(function (n) {
      ['data-src-start', 'data-src-end', 'data-src-line', 'data-fastmd-task-start', 'data-fastmd-task-end']
        .forEach(function (name) { parts.push(n.getAttribute(name)); });
    });
    return parts.join(',');
  }

  function children() { return Array.prototype.slice.call(el.children); }

  function report(ok, name, detail) {
    if (!ok) failures++;
    results.push((ok ? 'PASS  ' : 'FAIL  ') + name + (detail ? '   ' + detail : ''));
  }

  // Double-clicks the heading whose text is `probe.text` and returns the
  // source line the shell posts for it (what Go to Source / Ctrl+double-click
  // navigate to), or null when the heading is not on the page.
  function probeLine(probe) {
    var want = probe.text.replace(/[*_`~=]/g, '').trim();
    var hs = el.querySelectorAll('h1,h2,h3,h4,h5,h6');
    var h = null;
    for (var i = hs.length - 1; i >= 0; i--)
      if (hs[i].textContent.trim() === want) { h = hs[i]; break; }
    if (!h) return null;
    h.scrollIntoView({ block: 'center' });
    var target = h.querySelector('[data-src-line]') || h;
    var r = target.getBoundingClientRect();
    var before = window.__posted.length;
    target.dispatchEvent(new MouseEvent('dblclick', {
      bubbles: true, clientX: r.left + Math.min(4, r.width / 2), clientY: r.top + r.height / 2 }));
    for (var j = window.__posted.length - 1; j >= before; j--)
      if (window.__posted[j].type === 'previewFlash') return window.__posted[j].line;
    return 0;
  }

  // opts.maxInserted: the edit is local, so at most this many blocks may be
  // rebuilt. opts.probe: {text, line} checked by double-click after the edit.
  function runCase(name, base, next, opts) {
    opts = opts || {};
    var o = oracle(base, next);
    var before, beforeSig, calls;
    return push('<p>reset</p>')
      .then(function () { return push(base); })
      .then(function () {
        instrument();
        before = children();
        beforeSig = new Map(before.map(function (n) { return [n, lineSignature(n)]; }));
        hljsCalls = 0;
        return push(next);
      })
      .then(function () {
        calls = hljsCalls;
        var after = children();
        var problems = [];
        var retained = 0, shifted = 0;
        after.forEach(function (n) {
          if (!beforeSig.has(n)) return;
          retained++;
          if (beforeSig.get(n) !== lineSignature(n)) shifted++;
        });
        var inserted = after.length - retained;

        if (after.length !== o.newCount) problems.push('block count ' + after.length + ' vs ' + o.newCount);
        for (var k = 0; k < o.kept.length; k++) {
          if (after[o.kept[k][1]] !== before[o.kept[k][0]]) {
            problems.push('unchanged block ' + o.kept[k][1] + ' was rebuilt (identity lost)');
            break;
          }
        }
        if (opts.maxInserted !== undefined && inserted > opts.maxInserted)
          problems.push('rebuilt ' + inserted + ' blocks, expected <= ' + opts.maxInserted);
        if (opts.maxHighlight !== undefined && calls > opts.maxHighlight)
          problems.push('hljs.highlightElement=' + calls + ', expected <= ' + opts.maxHighlight);

        var probed = opts.probe ? probeLine(opts.probe) : undefined;
        if (opts.probe && probed !== null && probed !== opts.probe.line)
          problems.push('double-click "' + opts.probe.text + '" -> line ' + probed +
                        ', source line is ' + opts.probe.line);

        var live = el.innerHTML;
        return reference(next).then(function (ref) {
          var a = canon(live), b = canon(ref);
          if (a !== b) {
            var k = 0;
            while (k < a.length && k < b.length && a[k] === b[k]) k++;
            problems.push('DOM differs from fresh render @' + k +
                          '\n   live: ' + a.slice(Math.max(0, k - 80), k + 120) +
                          '\n   ref : ' + b.slice(Math.max(0, k - 80), k + 120));
          }
          report(!problems.length, name,
                 'blocks=' + o.newCount + ' retained=' + retained + ' rebuilt=' + inserted +
                 ' shiftedInPlace=' + shifted + ' delta=' + o.delta +
                 ' hljs=' + calls +
                 (opts.probe ? ' probe=' + (probed === null ? 'n/a' : probed) : '') +
                 (problems.length ? '\n   ' + problems.join('\n   ') : ''));
        });
      });
  }

  // ---- synthetic bodies --------------------------------------------------
  // Section i spans six source lines. `shift(i)` gives the extra lines in
  // front of section i; `edit` replaces section k's paragraph text.
  function body(n, shift, edit) {
    shift = shift || function () { return 0; };
    var out = [];
    for (var i = 0; i < n; i++) {
      var s = i * 6 + 1 + shift(i);
      out.push('<h2 data-src-start="' + s + '" data-src-end="' + s + '"><span data-src-line="' + s +
               '">Section ' + i + '</span></h2>');
      var text = edit && edit.at === i ? edit.text : 'Prose ' + i + ' with ';
      out.push('<p data-src-start="' + (s + 2) + '" data-src-end="' + (s + 3) + '"><span data-src-line="' +
               (s + 2) + '">' + text + '</span><strong><span data-src-line="' + (s + 3) +
               '">bold</span></strong></p>');
      if (i % 3 === 0)
        out.push('<pre data-src-start="' + (s + 4) + '" data-src-end="' + (s + 5) +
                 '"><code class="language-cpp"><span data-src-line="' + (s + 4) + '">int f' + i +
                 '();</span><span data-src-line="' + (s + 4) + '">\n</span></code></pre>');
      if (i % 4 === 0)
        out.push('<p data-src-start="' + (s + 5) + '" data-src-end="' + (s + 5) +
                 '"><span data-src-line="' + (s + 5) + '">Math $E_' + i + ' = mc^2$.</span></p>');
    }
    return out.join('\n');
  }
  var N = 120;
  // Section 61 is a heading and one paragraph (no code or math after it).
  var after61 = function (i) { return i > 61 ? 1 : 0; };

  var synthetic = [
    ['synthetic [enter splits a paragraph]', body(N), body(N, after61, { at: 61, text: 'Pro<br data-src-line="370">se 61 ' }),
      { maxInserted: 1, maxHighlight: 0 }],
    ['synthetic [blank line only: renumber, rebuild nothing]', body(N), body(N, function (i) { return i >= 30 ? 1 : 0; }),
      { maxInserted: 0, maxHighlight: 0 }],
    ['synthetic [backspace: delta -1]', body(N, function (i) { return i >= 10 ? 1 : 0; }), body(N),
      { maxInserted: 0, maxHighlight: 0 }],
    ['synthetic [edit near the beginning]', body(N), body(N, function (i) { return i > 1 ? 2 : 0; }, { at: 1, text: 'X ' }),
      { maxInserted: 1, maxHighlight: 0 }],
    ['synthetic [edit at last block]', body(N), body(N, null, { at: N - 1, text: 'X ' }), { maxInserted: 1 }],
    // Two different deltas: only the suffix with one consistent delta is kept;
    // the run with the other delta must be rebuilt, not mis-numbered.
    ['synthetic [inconsistent deltas fall back]', body(N),
      body(N, function (i) { return i >= 80 ? 2 : i >= 40 ? 1 : 0; })],
    // A block below the edit may also hold numbers from above it (a list whose
    // reference link is defined earlier): those stay, the rest move.
    ['synthetic [numbers above the edit stay put]',
      body(N) + '\n<p data-src-start="10" data-src-end="900"><span data-src-line="900">tail</span></p>',
      body(N, function (i) { return i >= 50 ? 1 : 0; }) +
        '\n<p data-src-start="10" data-src-end="901"><span data-src-line="901">tail</span></p>',
      { maxInserted: 0 }],
    // A number that stayed although a smaller one moved fits no single edit,
    // so that block is rebuilt rather than renumbered.
    ['synthetic [contradictory numbers fall back]',
      body(N) + '\n<p data-src-start="900" data-src-end="950"><span data-src-line="900">tail</span></p>',
      body(N, function (i) { return i >= 50 ? 1 : 0; }) +
        '\n<p data-src-start="901" data-src-end="950"><span data-src-line="901">tail</span></p>'],
    // Text that merely looks like the attribute is content, not a line number.
    ['synthetic [attribute-like text is content]',
      body(N) + '\n<p data-src-start="900"> data-src-line="5" </p>',
      body(N, function (i) { return i >= 50 ? 1 : 0; }) + '\n<p data-src-start="901"> data-src-line="5" </p>'],
    // ...even when it changes by exactly the shift: that is an edit to the text.
    ['synthetic [attribute-like text that changed is rebuilt]',
      body(N) + '\n<p data-src-start="900"> data-src-line="900" </p>',
      body(N, function (i) { return i >= 50 ? 1 : 0; }) + '\n<p data-src-start="901"> data-src-line="901" </p>'],
    ['synthetic [content change below the shift is rebuilt]', body(N),
      body(N, function (i) { return i >= 50 ? 1 : 0; }, { at: 100, text: 'Prose 100 changed ' })]
  ];

  var chain = Promise.resolve();
  synthetic.forEach(function (c) {
    chain = chain.then(function () { return runCase(c[0], c[1], c[2], c[3]); });
  });

  // A shifted block keeps its node across further pushes: renumber twice,
  // then make sure an ordinary edit after that still diffs correctly.
  chain = chain.then(function () {
    var b0 = body(N), b1 = body(N, function (i) { return i >= 20 ? 1 : 0; });
    var b2 = body(N, function (i) { return i >= 20 ? 3 : 0; });
    var b3 = body(N, function (i) { return i >= 20 ? 3 : 0; }, { at: 90, text: 'Z ' });
    var keep;
    return push(b0).then(function () { keep = el.children[el.children.length - 1]; return push(b1); })
      .then(function () { return push(b2); })
      .then(function () { return push(b3); })
      .then(function () { return reference(b3); })
      .then(function (ref) {
        var last = el.children[el.children.length - 1];
        report(last === keep && canon(el.innerHTML) === canon(ref), 'synthetic [repeated shifts then edit]',
               'lastBlockKept=' + (last === keep) + ' last data-src-start=' + last.getAttribute('data-src-start'));
      });
  });

  // ---- real bodies from shiftcases.py ------------------------------------
  // Edits the diff can keep local: everything but the edited block(s), the
  // [TOC] and the footnote list must be kept.
  var LOCAL = {
    'same-line-char': 1, 'enter-split-paragraph': 1, 'enter-blank-line-top': 1,
    'enter-blank-line-middle': 1, 'enter-blank-line-end': 1, 'backspace-join-lines': 1,
    'insert-several-lines-middle': 2, 'backspace-merge-paragraphs': 1,
    'insert-paragraph-before-heading': 1, 'list-add-item': 1, 'list-split-item': 1,
    'blockquote-add-line': 1, 'table-add-row': 1, 'table-delete-row': 1,
    'code-add-line': 1, 'mermaid-add-line': 1, 'mermaid-insert-before': 1,
    'math-add-line': 1, 'footnote-blank-before': 1, 'heading-level-change': 2,
    'heading-insert': 3, 'insert-before-first-heading': 2
  };
  var real = window.__SHIFT_CASES || [];
  real.forEach(function (c) {
    chain = chain.then(function () {
      var kind = /\[(.*)\]$/.exec(c.name)[1];
      var opts = { probe: c.probe };
      if (LOCAL[kind] !== undefined) opts.maxInserted = LOCAL[kind] + 2;  // + [TOC], footnotes
      return runCase(c.name, c.base, c.next, opts);
    });
  });

  chain.then(function () {
    results.unshift('synthetic=' + (synthetic.length + 1) + ' real=' + real.length +
                    (real.length ? '' : '  (no shiftdata.js: real-document cases skipped)'));
    finish();
  }).catch(function (e) {
    failures++;
    results.push('FAIL  exception: ' + String((e && e.stack) || e));
    finish();
  });

  function finish() {
    var box = document.createElement('pre');
    box.id = 'shifttest-results';
    box.textContent = 'SHIFTTEST_BEGIN\n' + results.join('\n') +
                      '\nSHIFTTEST_VERDICT ' + (failures ? failures + ' FAILED' : 'ALL PASSED') +
                      '\nSHIFTTEST_END';
    document.body.appendChild(box);
    document.title = failures ? ('FAIL ' + failures) : 'ALL PASSED';
  }
})();
