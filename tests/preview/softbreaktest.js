// C++-generated bodies checked against independent semantic expectations.
// Compare DOMs without collapsing whitespace: a newline must never be hidden
// by normalization or confused with a visible <br>.
(async function () {
  var results = [], failures = 0, seq = 0, waiting;
  var el = document.getElementById('content');
  var orig = window.__posted.push.bind(window.__posted);
  window.__posted.push = function (m) {
    orig(m);
    if (m.type === 'rendered' && m.seq === seq && waiting) {
      var done = waiting; waiting = null; done();
    }
  };
  function push(html) {
    return new Promise(function (resolve) {
      waiting = resolve;
      window.__send({ type: 'content', html: html, seq: ++seq });
    });
  }
  function parse(html) {
    var template = document.createElement('template');
    template.innerHTML = html;
    return template.content;
  }
  function blocks(node) {
    // Incremental rendering keeps top-level elements, not the whitespace
    // between them. Preserve every character *inside* each block.
    return Array.from(node.children).map(function (n) { return n.outerHTML; }).join('');
  }
  function semanticDOM() {
    var clone = el.cloneNode(true);
    clone.lastElementChild.remove(); // Stable suffix used for identity checks.
    clone.querySelectorAll('span[data-src-line]').forEach(function (span) {
      if (span.attributes.length === 1 && !span.children.length)
        span.replaceWith.apply(span, Array.from(span.childNodes));
    });
    clone.querySelectorAll('*').forEach(function (node) {
      ['data-src-start', 'data-src-end', 'data-src-line'].forEach(function (attr) {
        node.removeAttribute(attr);
      });
    });
    return blocks(clone);
  }
  var suffix = '<h2 data-src-start="100" data-src-end="100">Stable suffix</h2>';
  try {
    var cases = window.__SOFT_BREAK_CASES;
    if (!cases || !cases.length) throw Error('missing C++ fixtures');
    for (var c of cases) {
      await push('');
      await push(c.html + suffix);
      var full = blocks(el);
      var expected = blocks(parse(c.expected));
      if (semanticDOM() !== expected) throw Error(c.name + ': full semantic DOM mismatch');
      // Also compare the entire mapped DOM against the C++ body as parsed by
      // the browser, so removing mappings above cannot conceal mapping loss.
      if (full !== blocks(parse(c.html + suffix)))
        throw Error(c.name + ': full mapped DOM mismatch');
      await push('<p>Different preceding block</p>' + suffix);
      var kept = el.lastElementChild;
      await push(c.html + suffix);
      if (blocks(el) !== full || semanticDOM() !== expected)
        throw Error(c.name + ': incremental DOM mismatch');
      if (el.lastElementChild !== kept) throw Error(c.name + ': suffix rebuilt');
      // Test the disabled/enabled transition directly as well as structural
      // edits; alternating cases are the same source with the setting toggled.
      results.push('PASS  ' + c.name + ' full/incremental/mapping/identity');
    }
    for (var c of cases) {
      await push(c.html + suffix);
      if (semanticDOM() !== blocks(parse(c.expected)))
        throw Error(c.name + ': setting transition mismatch');
    }
  } catch (e) {
    failures++;
    results.push('FAIL  ' + String(e.stack || e));
  }
  var box = document.createElement('pre');
  box.textContent = 'SOFTBREAKTEST_BEGIN\n' + results.join('\n') +
    '\nSOFTBREAKTEST_VERDICT ' + (failures ? failures + ' FAILED' : 'ALL PASSED') +
    '\nSOFTBREAKTEST_END';
  document.body.appendChild(box);
})();
