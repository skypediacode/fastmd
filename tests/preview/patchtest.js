// Changed-block transport: 'patch' messages from the host.
//
// Real cases (patchdata.js, see README) apply the exact patch the C++ side
// computes for each shiftcases edit and require: no refusal, every block the
// patch does not name is the same DOM object afterwards, the DOM (line
// attributes included) equals a fresh render of the new body, and a heading
// below the edit double-clicks to its true source line.
//
// Synthetic cases cover the protocol: stale base, count and fragment
// mismatches, an untrusted split, recovery through a whole-body resync,
// decoration of inserted blocks only, and a patch superseding another one's
// decoration.
(function () {
  var results = [];
  var failures = 0;
  var el = document.getElementById('content');
  var seq = 0;
  var waiters = {};
  var posted = window.__posted;
  var origPush = posted.push.bind(posted);
  posted.push = function (m) {
    origPush(m);
    if (m && m.type === 'rendered' && waiters[m.seq]) {
      var w = waiters[m.seq]; delete waiters[m.seq]; w();
    }
    return posted.length;
  };

  // Sends `msg` with the next seq; resolves on its 'rendered' (refused
  // patches never render, so callers check refusals synchronously).
  function send(msg) {
    msg.seq = ++seq;
    var p = new Promise(function (resolve) { waiters[msg.seq] = resolve; });
    window.__send(msg);
    return p;
  }
  function content(html, blocks) {
    var msg = { type: 'content', html: html };
    if (blocks !== undefined) msg.blocks = blocks;
    return send(msg);
  }
  function patch(fields, base) {
    var msg = { type: 'patch', base: base === undefined ? seq : base };
    for (var k in fields) msg[k] = fields[k];
    return send(msg);
  }
  function postedSince(n, type) {
    return posted.slice(n).filter(function (m) { return m.type === type; });
  }

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
        var done = false;
        var orig = w.__posted.push.bind(w.__posted);
        w.__posted.push = function (m) {
          orig(m);
          if (m && m.type === 'rendered' && !done) {
            done = true;
            var dom = w.document.getElementById('content').innerHTML;
            f.remove();
            resolve(dom);
          }
          return w.__posted.length;
        };
        w.__send({ type: 'content', html: html, seq: 1 });
      };
      f.src = 'patchtest.html?ref=1';
      document.body.appendChild(f);
    });
  }

  function sameAsFresh(html, problems) {
    var live = el.innerHTML;
    return reference(html).then(function (ref) {
      var a = canon(live), b = canon(ref);
      if (a !== b) {
        var k = 0;
        while (k < a.length && k < b.length && a[k] === b[k]) k++;
        problems.push('DOM differs from fresh render @' + k +
                      '\n   live: ' + a.slice(Math.max(0, k - 80), k + 120) +
                      '\n   ref : ' + b.slice(Math.max(0, k - 80), k + 120));
      }
    });
  }

  function report(ok, name, detail) {
    if (!ok) failures++;
    results.push((ok ? 'PASS  ' : 'FAIL  ') + name + (detail ? '   ' + detail : ''));
  }

  function children() { return Array.prototype.slice.call(el.children); }

  var hljsCalls = 0;
  function instrument() {
    if (window.hljs && !window.hljs.__wrapped) {
      var oh = window.hljs.highlightElement.bind(window.hljs);
      window.hljs.highlightElement = function (e) { hljsCalls++; return oh(e); };
      window.hljs.__wrapped = true;
    }
  }

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
    var before = posted.length;
    target.dispatchEvent(new MouseEvent('dblclick', {
      bubbles: true, clientX: r.left + Math.min(4, r.width / 2), clientY: r.top + r.height / 2 }));
    for (var j = posted.length - 1; j >= before; j--)
      if (posted[j].type === 'previewFlash') return posted[j].line;
    return 0;
  }

  // Old index -> new index for every block the patch keeps.
  function keptMapping(p) {
    var map = [], shift = 0, k = 0;
    for (var o = 0; o < p.oldCount; o++) {
      while (k < p.ops.length && p.ops[k][0] + p.ops[k][1] <= o) {
        shift += p.ops[k][2] - p.ops[k][1];
        k++;
      }
      if (k < p.ops.length && o >= p.ops[k][0] && o < p.ops[k][0] + p.ops[k][1]) continue;
      map.push([o, o + shift]);
    }
    return map;
  }

  // The C++ split must cut the body exactly where the browser does: every
  // block, parsed on its own, is the element the whole body parses into at
  // that index. (A patch inserts blocks parsed on their own.)
  function parseElements(html) {
    var t = document.createElement('template');
    t.innerHTML = html;
    var out = [];
    for (var n = t.content.firstChild; n; n = n.nextSibling) if (n.nodeType === 1) out.push(n.outerHTML);
    return out;
  }
  function splitParity(html, spans, label, problems) {
    if (!spans) return;
    var whole = parseElements(html);
    if (whole.length !== spans.length) {
      problems.push(label + ' split ' + spans.length + ' blocks, browser ' + whole.length);
      return;
    }
    for (var i = 0; i < spans.length; i++) {
      var alone = parseElements(html.slice(spans[i][0], spans[i][1]));
      if (alone.length !== 1 || alone[0] !== whole[i]) {
        problems.push(label + ' block ' + i + ' parses differently on its own');
        return;
      }
    }
  }

  // ---- real cases -------------------------------------------------------
  function runReal(c) {
    var problems = [];
    var before, mark;
    if (!c.patch) {
      report(true, c.name, 'unsplittable: whole-body path');
      return Promise.resolve();
    }
    splitParity(c.base, c.baseSpans, 'base', problems);
    splitParity(c.next, c.nextSpans, 'next', problems);
    return content('<p>reset</p>')
      .then(function () { return content(c.base, c.blocks[0]); })
      .then(function () {
        instrument();
        if (el.children.length !== c.blocks[0])
          problems.push('base split ' + c.blocks[0] + ' vs DOM ' + el.children.length);
        before = children();
        mark = posted.length;
        hljsCalls = 0;
        var done = patch(c.patch);
        var refused = postedSince(mark, 'resync');
        if (refused.length) { problems.push('patch refused'); return null; }
        return done;
      })
      .then(function () {
        var after = children();
        if (after.length !== c.blocks[1]) problems.push('block count ' + after.length + ' vs ' + c.blocks[1]);
        var map = keptMapping(c.patch);
        for (var k = 0; k < map.length; k++) {
          if (after[map[k][1]] !== before[map[k][0]]) {
            problems.push('kept block ' + map[k][0] + ' -> ' + map[k][1] + ' lost its node');
            break;
          }
        }
        var inserted = 0;
        c.patch.ops.forEach(function (op) { inserted += op[2]; });
        var probed = c.probe ? probeLine(c.probe) : undefined;
        if (c.probe && probed !== null && probed !== c.probe.line)
          problems.push('double-click "' + c.probe.text + '" -> line ' + probed + ', source line is ' + c.probe.line);
        if (postedSince(mark, 'blocksMismatch').length) problems.push('blocksMismatch');
        var payload = JSON.stringify(c.patch).length;
        return sameAsFresh(c.next, problems).then(function () {
          report(!problems.length, c.name,
                 'blocks=' + c.blocks[1] + ' kept=' + map.length + ' inserted=' + inserted +
                 ' renumbered=' + (c.patch.moved || []).reduce(function (s, r) { return s + r[1] - r[0] + 1; }, 0) +
                 ' payload=' + payload + '/' + c.next.length + ' hljs=' + hljsCalls +
                 (c.probe ? ' probe=' + (probed === null ? 'n/a' : probed) : '') +
                 (problems.length ? '\n   ' + problems.join('\n   ') : ''));
        });
      });
  }

  // ---- synthetic --------------------------------------------------------
  function sec(i, line, text) {
    return '<h2 data-src-start="' + line + '" data-src-end="' + line + '"><span data-src-line="' + line +
           '">Section ' + i + '</span></h2>\n<p data-src-start="' + (line + 2) + '" data-src-end="' + (line + 2) +
           '"><span data-src-line="' + (line + 2) + '">' + (text || 'Prose ' + i) + '</span></p>';
  }
  function doc(n, shiftFrom, delta, editAt, text) {
    var out = [];
    for (var i = 0; i < n; i++)
      out.push(sec(i, 4 * i + 1 + (i >= shiftFrom ? delta : 0), i === editAt ? text : null));
    return out.join('\n');
  }
  var N = 30;

  var synthetic = [];

  // A paragraph edited in place, then Enter below it (renumber 2 x 20 blocks).
  synthetic.push(function () {
    var name = 'synthetic [edit + line shift]';
    var problems = [];
    var base = doc(N);
    var next = doc(N, 10, 1, 9, 'Edited');
    var before;
    return content(base, 2 * N).then(function () {
      before = children();
      var mark = posted.length;
      var p = patch({ oldCount: 2 * N, blocks: 2 * N, shift: [41, 1], moved: [[20, 59]],
                      ops: [[19, 1, 1, '<p data-src-start="39" data-src-end="39"><span data-src-line="39">Edited</span></p>']] });
      if (postedSince(mark, 'resync').length) problems.push('refused');
      return p;
    }).then(function () {
      var after = children();
      for (var i = 0; i < 2 * N; i++)
        if (i !== 19 && after[i] !== before[i]) { problems.push('block ' + i + ' rebuilt'); break; }
      if (after[19] === before[19]) problems.push('edited block not replaced');
      return sameAsFresh(next, problems);
    }).then(function () { report(!problems.length, name, problems.join('\n   ')); });
  });

  // Refusals: nothing on the page may change, and the host is asked to resync.
  function refusal(name, makePatch, setup) {
    synthetic.push(function () {
      var problems = [];
      var base = doc(N);
      return (setup ? setup(base) : content(base, 2 * N)).then(function () {
        var html = el.innerHTML;
        var before = children();
        var mark = posted.length;
        patch(makePatch());
        var resyncs = postedSince(mark, 'resync');
        if (resyncs.length !== 1 || resyncs[0].seq !== seq) problems.push('no resync request for seq ' + seq);
        if (el.innerHTML !== html || children().some(function (n, i) { return n !== before[i]; }))
          problems.push('page changed by a refused patch');
        // The host answers with the whole body; that must recover fully.
        var next = doc(N, 5, 2, 3, 'After resync');
        return content(next, 2 * N).then(function () { return sameAsFresh(next, problems); });
      }).then(function () { report(!problems.length, name, problems.join('\n   ')); });
    });
  }
  var oneOp = function () {
    return { oldCount: 2 * N, blocks: 2 * N, ops: [[1, 1, 1, '<p data-src-start="3">X</p>']] };
  };
  refusal('synthetic [stale base is refused]', function () {
    var p = oneOp(); p.base = seq - 1; return p;
  });
  refusal('synthetic [old block count mismatch is refused]', function () {
    var p = oneOp(); p.oldCount = 2 * N + 1; return p;
  });
  refusal('synthetic [fragment parsing to other blocks is refused]', function () {
    var p = oneOp(); p.ops[0][3] = '<p>one</p><p>two</p>'; return p;
  });
  refusal('synthetic [fragment with bare text is refused]', function () {
    var p = oneOp(); p.ops[0][3] = 'text'; return p;
  });
  refusal('synthetic [patch on a whole-body push without a split is refused]', oneOp,
          function (base) { return content(base); });
  // The host's split disagreed with the browser: it is told, and no patch is trusted.
  refusal('synthetic [split mismatch reports and refuses]', oneOp, function (base) {
    var mark = posted.length;
    return content(base, 2 * N + 3).then(function () {
      if (postedSince(mark, 'blocksMismatch').length !== 1) throw new Error('no blocksMismatch');
    });
  });

  // A patch that fails part-way asks for the whole body; afterwards a resync
  // with prevBlocks dropped must still produce the right page.
  synthetic.push(function () {
    var name = 'synthetic [result count mismatch after applying is refused]';
    var problems = [];
    return content(doc(N), 2 * N).then(function () {
      var mark = posted.length;
      patch({ oldCount: 2 * N, blocks: 2 * N + 5, ops: [[1, 1, 1, '<p data-src-start="3">X</p>']] });
      if (postedSince(mark, 'resync').length !== 1) problems.push('no resync');
      var next = doc(N, 0, 0, 7, 'Recovered');
      return content(next, 2 * N).then(function () { return sameAsFresh(next, problems); });
    }).then(function () { report(!problems.length, name, problems.join('\n   ')); });
  });

  // Inserted code, math and a diagram are decorated; kept blocks are not
  // highlighted again.
  var codeDoc = function (extra) {
    var out = [];
    for (var i = 0; i < 6; i++)
      out.push(sec(i, 10 * i + 1) + '\n<pre data-src-start="' + (10 * i + 5) + '" data-src-end="' + (10 * i + 6) +
               '"><code class="language-cpp">int f' + i + '();\n</code></pre>');
    if (extra) out.splice(3, 0, extra);
    return out.join('\n');
  };
  var inserted = '<pre data-src-start="31" data-src-end="33"><code class="language-mermaid">graph LR\n  A --&gt; B\n</code></pre>\n' +
    '<pre data-src-start="34" data-src-end="35"><code class="language-python">x = 1\n</code></pre>\n' +
    '<p data-src-start="36" data-src-end="36">Mass \\(m^2\\) here.</p>';
  synthetic.push(function () {
    var name = 'synthetic [inserted blocks decorated, kept ones untouched]';
    var problems = [];
    var base = codeDoc(), next = codeDoc(inserted);
    var before;
    return content(base, 18).then(function () {
      instrument();
      hljsCalls = 0;
      before = children();
      return patch({ oldCount: 18, blocks: 21, ops: [[9, 0, 3, inserted]] });
    }).then(function () {
      if (hljsCalls !== 1) problems.push('hljs.highlightElement=' + hljsCalls + ', expected 1');
      var after = children();
      for (var i = 0; i < 18; i++)
        if (after[i < 9 ? i : i + 3] !== before[i]) { problems.push('kept block ' + i + ' rebuilt'); break; }
      if (!el.querySelector('.mermaid-diagram')) problems.push('diagram not drawn');
      if (!el.querySelector('.katex')) problems.push('math not typeset');
      return sameAsFresh(next, problems);
    }).then(function () { report(!problems.length, name, problems.join('\n   ')); });
  });

  // A second patch lands before the first one's decoration: the first one's
  // blocks are kept by it and must still end decorated.
  synthetic.push(function () {
    var name = 'synthetic [patch superseding a decoration pass]';
    var problems = [];
    var base = codeDoc(), mid = codeDoc(inserted);
    var next = mid.replace('>Prose 5<', '>Prose 5 edited<');
    return content(base, 18).then(function () {
      patch({ oldCount: 18, blocks: 21, ops: [[9, 0, 3, inserted]] });
      return patch({ oldCount: 21, blocks: 21, ops: [[19, 1, 1,
        '<p data-src-start="53" data-src-end="53"><span data-src-line="53">Prose 5 edited</span></p>']] });
    }).then(function () {
      if (!el.querySelector('.mermaid-diagram')) problems.push('diagram from the first patch not drawn');
      if (el.querySelector('code.language-python:not([data-highlighted])')) problems.push('code not highlighted');
      if (!el.querySelector('.katex')) problems.push('math not typeset');
      return sameAsFresh(next, problems);
    }).then(function () { report(!problems.length, name, problems.join('\n   ')); });
  });

  var chain = Promise.resolve();
  synthetic.forEach(function (fn) { chain = chain.then(fn); });
  var real = window.__PATCH_CASES || [];
  real.forEach(function (c) { chain = chain.then(function () { return runReal(c); }); });

  chain.then(function () {
    results.unshift('synthetic=' + synthetic.length + ' real=' + real.length +
                    (real.length ? '' : '  (no patchdata.js: real-document cases skipped)'));
    finish();
  }).catch(function (e) {
    failures++;
    results.push('FAIL  exception: ' + String((e && e.stack) || e));
    finish();
  });

  function finish() {
    var box = document.createElement('pre');
    box.id = 'patchtest-results';
    box.textContent = 'PATCHTEST_BEGIN\n' + results.join('\n') +
                      '\nPATCHTEST_VERDICT ' + (failures ? failures + ' FAILED' : 'ALL PASSED') +
                      '\nPATCHTEST_END';
    document.body.appendChild(box);
    document.title = failures ? ('FAIL ' + failures) : 'ALL PASSED';
  }
})();
