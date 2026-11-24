// Control experiment: render the SAME body from scratch in two independent
// fresh iframes and compare. Any difference is the renderer's own
// nondeterminism, not the incremental diff.
(function () {
  var out = [];

  function pusherFor(win) {
    var waiters = {}, seq = 0;
    var posted = win.__posted;
    var origPush = posted.push.bind(posted);
    posted.push = function (m) {
      origPush(m);
      if (m && m.type === 'rendered' && waiters[m.seq]) { var w = waiters[m.seq]; delete waiters[m.seq]; w(); }
      return posted.length;
    };
    return function (html) {
      var want = ++seq;
      return new Promise(function (r) { waiters[want] = r; win.__send({ type: 'content', html: html, seq: want }); });
    };
  }

  function canon(s) {
    return s.replace(/fastmd-mmd-\d+/g, 'MMD')
            .replace(/\bid="[^"]*(mermaid|katex|mmd)[^"]*"/gi, 'id="X"')
            .replace(/\bfor="[^"]*(mermaid|katex|mmd)[^"]*"/gi, 'for="X"')
            .replace(/aria-owns="[^"]*"/g, '')
            .replace(/\s+/g, ' ').trim();
  }

  function freshRender(html) {
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
      f.src = 'realsamples.html?ref=1';
      document.body.appendChild(f);
    });
  }

  var targets = (window.__SAMPLES || []).filter(function (s) {
    return s.name === 'mermaid.md' || s.name === 'test-all-features.md';
  });

  var chain = Promise.resolve();
  targets.forEach(function (s) {
    chain = chain.then(function () {
      return freshRender(s.html).then(function (a) {
        return freshRender(s.html).then(function (b) {
          var ca = canon(a), cb = canon(b);
          var same = ca === cb;
          var k = 0;
          while (k < ca.length && k < cb.length && ca[k] === cb[k]) k++;
          out.push((same ? 'DETERMINISTIC  ' : 'NONDETERMINISTIC ') + s.name +
                   (same ? '' : '  first difference between two from-scratch renders @' + k));
          if (!same) {
            out.push('   renderA: ' + ca.slice(Math.max(0, k - 70), k + 110));
            out.push('   renderB: ' + cb.slice(Math.max(0, k - 70), k + 110));
          }
        });
      });
    });
  });

  chain.then(function () {
    var box = document.createElement('pre');
    box.id = 'control-results';
    box.textContent = 'CONTROL_BEGIN\n' + out.join('\n') + '\nCONTROL_END';
    document.body.appendChild(box);
  });
})();
