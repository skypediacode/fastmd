// Drives the preview shell through content pushes and checks that the
// incremental (diffed) DOM matches a from-scratch render of the same body.
(function () {
  var results = [];

  // Purely promise-driven: resolve when the shell posts {type:'rendered'} for
  // our seq. No polling, so Chrome's virtual-time clock can fast-forward.
  function pusherFor(win) {
    var waiters = {};
    var seq = 0;
    var posted = win.__posted;
    var origPush = posted.push.bind(posted);
    posted.push = function (m) {
      origPush(m);
      if (m && m.type === 'rendered' && waiters[m.seq]) {
        var w = waiters[m.seq];
        delete waiters[m.seq];
        w(win.performance.now());
      }
      return posted.length;
    };
    return function (html) {
      var want = ++seq;
      var start = win.performance.now();
      return new Promise(function (resolve) {
        waiters[want] = function (end) { resolve(end - start); };
        win.__send({ type: 'content', html: html, seq: want });
      });
    };
  }

  // KaTeX and Mermaid mint fresh ids per render; normalize them away.
  function canon(s) {
    return s
      .replace(/fastmd-mmd-\d+/g, 'MMD')
      .replace(/\bid="[^"]*(mermaid|katex)[^"]*"/gi, 'id="X"')
      .replace(/aria-owns="[^"]*"/g, '')
      .replace(/\s+/g, ' ')
      .trim();
  }

  function check(name, cond, extra) {
    results.push((cond ? 'PASS  ' : 'FAIL  ') + name + (cond ? '' : '   ' + (extra || '')));
  }

  function body(n, opts) {
    opts = opts || {};
    var out = [];
    for (var i = 0; i < n; i++) {
      out.push('<h2 data-src-start="' + (i * 6 + 1) + '">Section ' + i + (opts.all ? ' X' : '') + '</h2>');
      out.push('<p data-src-start="' + (i * 6 + 2) + '">Prose ' + i +
               (opts.edited === i ? ' EDITED' : '') + ' with <strong>bold</strong>.</p>');
      if (i % 3 === 0)
        out.push('<pre><code class="language-cpp">int f' + i + '(int x){return x*' + i + (opts.all ? '+1' : '') + ';}</code></pre>');
      if (i % 6 === 0)
        out.push('<pre><code>Plain prose must not be auto-detected as a programming language.</code></pre>');
      if (i % 4 === 0)
        out.push('<p>Math \\(E_' + i + ' = mc^2\\) inline.</p>');
      if (i % 5 === 0)
        out.push('<blockquote class="fastmd-callout fastmd-callout-note"><p>Note ' + i + '</p></blockquote>');
      if (i % 7 === 0)
        out.push('<ul data-fastmd-task-list="1"><li data-fastmd-task-item="1">' +
                 '<input type="checkbox" disabled> task ' + i + '</li></ul>');
    }
    return out.join('\n');
  }

  var N = 120;
  var A = body(N);
  var B = body(N, { edited: 60 });   // one paragraph changed mid-document
  var C = body(N + 1);               // a section appended
  var D = body(N - 1);               // a section removed

  var push = pusherFor(window);
  var el = document.getElementById('content');
  var domA, tA, tB;

  // Count the expensive per-update work so the saving is measurable
  // independently of the (virtualized) headless clock.
  var work = { highlight: 0, math: 0 };
  function instrument() {
    if (window.hljs && !window.hljs.__wrapped) {
      var oh = window.hljs.highlightElement.bind(window.hljs);
      window.hljs.highlightElement = function (e) { work.highlight++; return oh(e); };
      window.hljs.__wrapped = true;
    }
    if (window.renderMathInElement && !window.renderMathInElement.__wrapped) {
      var om = window.renderMathInElement;
      window.renderMathInElement = function (r, o) { work.math++; return om(r, o); };
      window.renderMathInElement.__wrapped = true;
    }
  }
  function snapshotWork() { return { highlight: work.highlight, math: work.math }; }

  push(A).then(function (t) {
    tA = t;
    domA = el.innerHTML;
    check('initial render produced content', domA.length > 1000, 'len=' + domA.length);
    check('code highlighted', /class="[^"]*hljs/.test(domA), 'no hljs');
    check('language badge added', /code-lang-badge/.test(domA), 'no badge');
    var plainCode = el.querySelector('pre > code:not([class])');
    check('unlabelled code remains plain', !!plainCode && !plainCode.hasAttribute('data-highlighted') &&
          !plainCode.parentNode.parentNode.classList.contains('code-block-wrapper'), 'auto-detected or badged');
    check('katex typeset', /class="katex/.test(domA), 'no katex');
    check('all sections present', /Section 119/.test(domA), 'missing tail');
    instrument();
    return push(A);
  }).then(function () {
    check('identical re-push leaves DOM byte-identical', el.innerHTML === domA, 'DOM changed');
    return push(B);
  }).then(function (t) {
    tB = t;
    var domB = el.innerHTML;
    check('edit applied', /Prose 60 EDITED/.test(domB), 'edit missing');
    check('incremental update faster than full render', tB < tA,
          'incremental=' + tB.toFixed(1) + ' full=' + tA.toFixed(1));
    results.push('WORK one-block-edit:  hljs.highlightElement=' + work.highlight +
                 '  katex passes=' + work.math);
    check('decorations intact after edit',
          /class="[^"]*hljs/.test(domB) && /class="katex/.test(domB) && /code-lang-badge/.test(domB),
          'lost decorations');
    var badges = (domB.match(/code-lang-badge/g) || []).length;
    var wrappers = (domB.match(/code-block-wrapper/g) || []).length;
    check('no duplicated badges/wrappers', badges === wrappers,
          'badges=' + badges + ' wrappers=' + wrappers);
    check('no double-highlight artifacts',
          !/hljs[^"]*hljs/.test(domB) && (domB.match(/data-highlighted/g) || []).length ===
            (domB.match(/<code class="language-/g) || []).length, 'suspicious highlight markup');
    work.highlight = 0; work.math = 0;
    return push(body(N, { edited: -1, all: true })).then(function () {
      results.push('WORK whole-document change: hljs.highlightElement=' + work.highlight +
                   '  katex passes=' + work.math);
      return push(B);
    });
  }).then(function () {
    return push(C);
  }).then(function () {
    check('append rendered', /Section 120/.test(el.innerHTML), 'append missing');
    return push(D);
  }).then(function () {
    var d = el.innerHTML;
    check('removal applied', !/Section 119/.test(d) && /Section 118/.test(d), 'removal failed');
    return push(B);
  }).then(function () {
    // Reference: same body rendered from scratch in a fresh iframe.
    var live = el.innerHTML;
    return new Promise(function (resolve, reject) {
      var f = document.createElement('iframe');
      f.style.cssText = 'position:absolute;left:-9999px;width:900px;height:600px';
      f.onload = function () {
        try {
          var w = f.contentWindow;
          pusherFor(w)(B).then(function () {
            resolve([live, w.document.getElementById('content').innerHTML]);
          });
        } catch (e) { reject(e); }
      };
      f.src = 'difftest.html?ref=1';
      document.body.appendChild(f);
    });
  }).then(function (pair) {
    var live = canon(pair[0]);
    var ref = canon(pair[1]);
    check('incremental DOM === from-scratch DOM', live === ref,
          'len ' + live.length + ' vs ' + ref.length);
    if (live !== ref) {
      var k = 0;
      while (k < live.length && k < ref.length && live[k] === ref[k]) k++;
      results.push('       divergence@' + k);
      results.push('       live: ' + live.slice(Math.max(0, k - 100), k + 160));
      results.push('       ref : ' + ref.slice(Math.max(0, k - 100), k + 160));
    }
    finish();
  }).catch(function (e) {
    check('no exception thrown', false, String((e && e.stack) || e));
    finish();
  });

  function finish() {
    var failed = results.filter(function (r) { return r.indexOf('FAIL') === 0; }).length;
    var box = document.createElement('pre');
    box.id = 'difftest-results';
    box.textContent = 'DIFFTEST_BEGIN\n' + results.join('\n') +
                      '\nDIFFTEST_VERDICT ' + (failed ? failed + ' FAILED' : 'ALL PASSED') +
                      '\nDIFFTEST_END';
    document.body.appendChild(box);
    document.title = failed ? ('FAIL ' + failed) : 'ALL PASSED';
  }
})();
