// Real C++ parser output, bundled KaTeX/CSS, the live host message protocol,
// and the actual standalone/print shells. DOM equality alone cannot catch
// misplaced scripts: also measure brace/label geometry after fonts load.
(async function () {
  const results = [];
  let failures = 0, seq = 0;
  const waiters = new Map();
  const push = window.__posted.push.bind(window.__posted);
  window.__posted.push = function (m) {
    push(m);
    if (m.type === 'rendered' && waiters.has(m.seq)) {
      waiters.get(m.seq)(); waiters.delete(m.seq);
    }
    return this.length;
  };
  function check(ok, name) {
    results.push((ok ? 'PASS ' : 'FAIL ') + name);
    if (!ok) ++failures;
  }
  async function send(fields) {
    const next = ++seq;
    const done = new Promise(resolve => waiters.set(next, resolve));
    window.__send(Object.assign({seq: next}, fields));
    await done;
    await document.fonts.ready;
  }
  function formulas(doc) { return [...doc.querySelectorAll('#content .katex-html')]; }
  function measure(doc, name) {
    const rendered = formulas(doc);
    check(rendered.length === 6 && !doc.querySelector('.katex-error'), name + ' all constructs typeset');
    for (let i = 0; i < 6; i += 2) {
      check(rendered[i]?.innerHTML === rendered[i + 1]?.innerHTML,
            name + ' standard/copy pair ' + (i / 2 + 1) + ' identical');
    }
    for (const equation of rendered.slice(0, 2)) {
      const labels = [...equation.querySelectorAll('.mord.text')];
      const braces = [...equation.querySelectorAll('.stretchy')];
      check(labels.length === 3 && braces.length === 3, name + ' three labels and braces');
      labels.forEach((label, i) => {
        const l = label.getBoundingClientRect(), b = braces[i].getBoundingClientRect();
        check(l.top >= b.bottom - 1, name + ' label ' + (i + 1) + ' below brace');
        // Labels include a final math variable outside \text; the containing
        // script is centered, so use its inline box rather than text alone.
        const script = label.parentElement.getBoundingClientRect();
        check(Math.abs((script.left + script.right - b.left - b.right) / 2) < 2,
              name + ' script ' + (i + 1) + ' centered');
        const display = equation.closest('.katex-display').getBoundingClientRect();
        check(l.top >= display.top && l.bottom <= display.bottom + 1,
              name + ' label ' + (i + 1) + ' not clipped');
      });
      const text = equation.textContent.replace(/\u00a0/g, '');
      check(text.includes('Độthayđổicủa') && text.includes('Độnghi'), name + ' Vietnamese labels retained');
    }
  }
  try {
    if (!window.__MATH_LAYOUT) throw new Error('Generate mathlayoutdata.js first');
    const data = window.__MATH_LAYOUT;
    for (const dark of [false, true]) {
      window.__send({type: 'theme', dark, fs: 17});
      await send({type: 'content', html: data.base});
      measure(document, dark ? 'dark fresh' : 'light fresh');
      const before = document.getElementById('content').lastElementChild;
      await send({type: 'content', html: data.edited});
      check(document.getElementById('content').lastElementChild === before,
            'edit retains unchanged formula');
      // The first expression changed; its matching copy changes too.
      measure(document, dark ? 'dark edited' : 'light edited');
      await send({type: 'content', html: data.base, blocks: data.count});
      const baseline = seq;
      await send(Object.assign({type: 'patch', base: baseline}, data.patch));
      measure(document, dark ? 'dark patch' : 'light patch');
    }
    for (const [name, url] of Object.entries(data.exports)) {
      const frame = document.createElement('iframe');
      frame.style.cssText = 'width:1200px;height:700px;border:0';
      const loaded = new Promise(resolve => frame.onload = resolve);
      frame.src = url; document.body.append(frame); await loaded;
      const doc = frame.contentDocument;
      // Standalone shell exposes the same decoration readiness used by PDF.
      if (!frame.contentWindow.fastmdPreviewRendered) {
        await new Promise(resolve => frame.contentWindow.addEventListener(
          'fastmd-preview-rendered', resolve, {once: true}));
      }
      await doc.fonts.ready;
      measure(doc, name);
      frame.remove();
    }
  } catch (e) { check(false, e.stack || String(e)); }
  const out = document.createElement('pre'); out.id = 'mathlayout-results';
  out.textContent = 'MATHLAYOUT_BEGIN\n' + results.join('\n') +
    '\nMATHLAYOUT_VERDICT ' + (failures ? failures + ' FAILED' : 'ALL PASSED') + '\nMATHLAYOUT_END';
  document.body.append(out);
})();
