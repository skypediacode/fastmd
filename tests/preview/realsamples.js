// Runs every real sample document through the incremental renderer and checks
// that the resulting DOM matches a from-scratch render of the same body.
//
// For each document it applies the three edit shapes the diff must handle:
// a changed block in the middle, an appended block, and a removed first block.
// window.__SAMPLES is generated from samples/*.md (see README).
(function () {
  var results = [];
  var failures = 0;

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

  // Mermaid mints fresh element ids per render AND draws edges with randomized
  // bezier control points: two from-scratch renders of the same source differ.
  // (control.js demonstrates this independently of the diff.) So SVG subtrees
  // are collapsed to a placeholder — svgCount() below still asserts that the
  // same number of diagrams exists, so a dropped or duplicated one fails.
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

  function svgCount(s) { return (s.match(/<svg/gi) || []).length; }

  function blocksOf(html) {
    var tpl = document.createElement('template');
    tpl.innerHTML = html;
    var out = [];
    for (var n = tpl.content.firstChild; n; n = n.nextSibling)
      if (n.nodeType === 1) out.push(n.outerHTML);
    return out;
  }

  function variants(html) {
    var b = blocksOf(html);
    if (b.length < 3) return null;
    var mid = Math.floor(b.length / 2);

    var changed = b.slice();
    changed[mid] = changed[mid].replace(/>([^<>]{3,})</, '>$1 MUTATED<');
    if (changed[mid] === b[mid]) changed[mid] = '<p>MUTATED</p>';

    return {
      changed: changed.join('\n'),
      appended: b.concat('<p>APPENDED BLOCK</p>').join('\n'),
      removed: b.slice(1).join('\n')
    };
  }

  var push = pusherFor(window);
  var el = document.getElementById('content');

  // A fresh iframe renders `html` from scratch for comparison.
  function reference(html) {
    return new Promise(function (resolve, reject) {
      var f = document.createElement('iframe');
      f.style.cssText = 'position:absolute;left:-9999px;width:900px;height:600px';
      f.onload = function () {
        try {
          var w = f.contentWindow;
          pusherFor(w)(html).then(function () {
            var dom = w.document.getElementById('content').innerHTML;
            f.remove();
            resolve(dom);
          });
        } catch (e) { reject(e); }
      };
      f.src = 'realsamples.html?ref=1';
      document.body.appendChild(f);
    });
  }

  function step(name, base, next) {
    // Establish `base` incrementally, then apply `next` incrementally.
    return push(base)
      .then(function () { return push(next); })
      .then(function () {
        var rawLive = el.innerHTML;
        var live = canon(rawLive);
        return reference(next).then(function (rawRef) {
          var ref = canon(rawRef);
          if (svgCount(rawLive) !== svgCount(rawRef)) {
            failures++;
            results.push('FAIL  ' + name + '  diagram count ' +
                         svgCount(rawLive) + ' vs ' + svgCount(rawRef));
            return;
          }
          if (live === ref) return;
          failures++;
          var k = 0;
          while (k < live.length && k < ref.length && live[k] === ref[k]) k++;
          results.push('FAIL  ' + name + '  @' + k);
          results.push('   live: ' + live.slice(Math.max(0, k - 80), k + 120));
          results.push('   ref : ' + ref.slice(Math.max(0, k - 80), k + 120));
        });
      });
  }

  var samples = window.__SAMPLES || [];
  var chain = Promise.resolve();
  var checked = 0, skipped = 0;

  samples.forEach(function (s) {
    chain = chain.then(function () {
      var v = variants(s.html);
      if (!v) { skipped++; return; }
      return step(s.name + ' [changed]', s.html, v.changed)
        .then(function () { return step(s.name + ' [appended]', s.html, v.appended); })
        .then(function () { return step(s.name + ' [removed]', s.html, v.removed); })
        .then(function () { checked++; });
    });
  });

  chain.then(function () {
    results.unshift('documents checked=' + checked + ' skipped(<3 blocks)=' + skipped +
                    ' comparisons=' + (checked * 3));
    finish();
  }).catch(function (e) {
    failures++;
    results.push('FAIL  exception: ' + String((e && e.stack) || e));
    finish();
  });

  function finish() {
    var box = document.createElement('pre');
    box.id = 'realsamples-results';
    box.textContent = 'REALSAMPLES_BEGIN\n' + results.join('\n') +
                      '\nREALSAMPLES_VERDICT ' +
                      (failures ? failures + ' FAILED' : 'ALL PASSED') +
                      '\nREALSAMPLES_END';
    document.body.appendChild(box);
    document.title = failures ? ('FAIL ' + failures) : 'ALL PASSED';
  }
})();
