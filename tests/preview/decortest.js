// Decoration lifecycle: a push that supersedes an unfinished decoration pass
// must not strand the blocks that pass was decorating. The superseding push
// usually keeps those blocks (they are unchanged), so unless something
// remembers them they stay as raw code, raw $...$ and raw Mermaid source.
//
// Every case pushes A and then B before A's decoration can finish, waits for
// B, and requires the page to equal a fresh render of B with every code block
// highlighted, every formula typeset and every diagram drawn.
(function () {
  if (location.search.indexOf('ref=1') !== -1) return;
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
    // send() pushes without waiting; push() resolves once that push reports
    // 'rendered' (only the newest push ever does).
    function send(msg) {
      var want = ++seq;
      msg.seq = want;
      var p = new Promise(function (resolve) { waiters[want] = resolve; });
      win.__send(msg);
      return p;
    }
    return send;
  }
  var send = pusherFor(window);
  function content(html) { return send({ type: 'content', html: html }); }

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
        var s = pusherFor(w);
        s({ type: 'content', html: html }).then(function () {
          var dom = w.document.getElementById('content').innerHTML;
          f.remove();
          resolve(dom);
        });
      };
      f.src = 'decortest.html?ref=1';
      document.body.appendChild(f);
    });
  }

  function report(ok, name, detail) {
    if (!ok) failures++;
    results.push((ok ? 'PASS  ' : 'FAIL  ') + name + (detail ? '   ' + detail : ''));
  }

  // What a fully decorated page has: no labelled code left unhighlighted, no
  // Mermaid source left undrawn, no inline formula left as text.
  function undecorated() {
    var out = [];
    var codes = el.querySelectorAll('pre > code[class*="language-"]');
    for (var i = 0; i < codes.length; i++) {
      if (/language-mermaid/.test(codes[i].className)) { out.push('mermaid source'); continue; }
      if (codes[i].getAttribute('data-highlighted') !== 'yes') out.push('code ' + codes[i].className);
      else if (!codes[i].parentNode.parentNode.classList.contains('code-block-wrapper')) out.push('badge');
    }
    var walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
    for (var n; (n = walker.nextNode());) {
      if (n.parentNode.closest('.katex, pre, code')) continue;
      if (/\$[^$\s][^$]*\$/.test(n.nodeValue)) out.push('math "' + n.nodeValue.trim().slice(0, 30) + '"');
    }
    return out;
  }

  function block(i, extra) {
    var s = i * 10 + 1;
    var out = '<h2 data-src-start="' + s + '" data-src-end="' + s + '">Section ' + i + '</h2>' +
      '<p data-src-start="' + (s + 2) + '" data-src-end="' + (s + 2) + '">Prose ' + i +
      ' with $x_' + i + '^2$ inline.</p>' +
      '<pre data-src-start="' + (s + 4) + '" data-src-end="' + (s + 6) +
      '"><code class="language-cpp">int f' + i + '() { return ' + i + '; }\n</code></pre>';
    if (i % 4 === 0)
      out += '<pre data-src-start="' + (s + 7) + '" data-src-end="' + (s + 9) +
        '"><code class="language-mermaid">graph TD\n  A' + i + ' --&gt; B' + i + '\n</code></pre>';
    return out + (extra || '');
  }
  function doc(n, opts) {
    opts = opts || {};
    var out = [];
    for (var i = 0; i < n; i++)
      out.push(i === opts.at ? opts.replace : block(i, i === opts.after ? opts.extra : ''));
    return out.join('\n');
  }
  var N = 12;
  var edited = { at: 5, replace: '<p data-src-start="51" data-src-end="51">Edited $y^3$ paragraph.</p>' };
  var inserted = { after: 7, extra:
    '<pre data-src-start="78" data-src-end="80"><code class="language-mermaid">graph LR\n  New --&gt; Node\n</code></pre>' +
    '<pre data-src-start="81" data-src-end="82"><code class="language-python">def added(): return 1\n</code></pre>' +
    '<p data-src-start="83" data-src-end="83">New $z_1$ formula.</p>' };
  var insertedEdited = { after: 7, extra: inserted.extra, at: 2,
    replace: '<p data-src-start="21" data-src-end="21">Changed again.</p>' };

  function check(name, html) {
    var problems = undecorated();
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
      report(!problems.length, name, problems.length ? '\n   ' + problems.slice(0, 8).join('\n   ') : '');
    });
  }

  var chain = Promise.resolve();

  // 1. First push of the page: highlight.js, KaTeX and Mermaid are not even
  // loaded yet when the second push keeps every block but one.
  chain = chain.then(function () {
    var a = doc(N), b = doc(N, edited);
    content(a);
    return content(b).then(function () { return check('cold assets: first push superseded', b); });
  });

  // 2. Assets warm: A inserts code, math and a diagram; B (same task) only
  // edits a block elsewhere, so it keeps A's new blocks.
  chain = chain.then(function () {
    var base = doc(N), a = doc(N, inserted), b = doc(N, insertedEdited);
    return content('<p>reset</p>').then(function () { return content(base); }).then(function () {
      content(a);
      return content(b);
    }).then(function () { return check('warm: inserted blocks kept by the superseding push', b); });
  });

  // 3. B arrives while A's Mermaid render is in flight, not before it starts.
  chain = chain.then(function () {
    var base = doc(N), a = doc(N, inserted), b = doc(N, insertedEdited);
    return content('<p>reset</p>').then(function () { return content(base); }).then(function () {
      content(a);
      return new Promise(function (r) { setTimeout(r, 0); }).then(function () { return content(b); });
    }).then(function () { return check('warm: superseded during the Mermaid render', b); });
  });

  // 4. The superseding push is identical (a tab activation re-push): it
  // changes no block at all, yet A's blocks still need decorating.
  chain = chain.then(function () {
    var base = doc(N), a = doc(N, inserted);
    return content('<p>reset</p>').then(function () { return content(base); }).then(function () {
      content(a);
      return content(a);
    }).then(function () { return check('warm: identical superseding push', a); });
  });

  // 5. A chain of supersessions: every block A..C introduced must end decorated.
  chain = chain.then(function () {
    var base = doc(N);
    var a = doc(N, inserted);
    var b = doc(N, insertedEdited);
    // Each push edits one block's text in place, so the element count never
    // changes and A's inserted blocks are kept by both B and C.
    var c = b.replace('Prose 10 with', 'Prose 10 now with');
    return content('<p>reset</p>').then(function () { return content(base); }).then(function () {
      content(a);
      content(b);
      return content(c);
    }).then(function () { return check('warm: three pushes in a row', c); });
  });

  // 6. A block A inserted and B then replaced is never decorated needlessly
  // and never resurrected.
  chain = chain.then(function () {
    var base = doc(N), a = doc(N, inserted), b = doc(N);
    return content('<p>reset</p>').then(function () { return content(base); }).then(function () {
      content(a);
      return content(b);
    }).then(function () { return check('warm: superseding push removes the new blocks', b); });
  });

  chain.then(finish).catch(function (e) {
    failures++;
    results.push('FAIL  exception: ' + String((e && e.stack) || e));
    finish();
  });

  function finish() {
    var box = document.createElement('pre');
    box.id = 'decortest-results';
    box.textContent = 'DECORTEST_BEGIN\n' + results.join('\n') +
                      '\nDECORTEST_VERDICT ' + (failures ? failures + ' FAILED' : 'ALL PASSED') +
                      '\nDECORTEST_END';
    document.body.appendChild(box);
    document.title = failures ? ('FAIL ' + failures) : 'ALL PASSED';
  }
})();
