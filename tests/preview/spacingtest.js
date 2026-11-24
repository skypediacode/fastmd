// Document Spacing in the real preview shell: the 'spacing' message, computed
// styles and real layout geometry at every preset, zero spacing, line-height 1.0
// with inline code/mark/kbd/math, and that headings, code blocks and math keep
// their own spacing. The C++ side (CSS strings, settings, export HTML) is covered
// by DocumentSpacingTest; the strings below mirror DocumentSpacing.h.
(async function () {
  var results = [], failures = 0, seq = 0;
  var waiters = {};
  var push = window.__posted.push.bind(window.__posted);
  window.__posted.push = function (m) {
    push(m);
    if (m.type === 'rendered' && waiters[m.seq]) { waiters[m.seq](); delete waiters[m.seq]; }
    return this.length;
  };
  function pass(name) { results.push('PASS  ' + name); }
  function fail(name) { failures++; results.push('FAIL  ' + name); }
  function expect(ok, name) { ok ? pass(name) : fail(name); }
  function near(a, b, tol) { return Math.abs(a - b) <= (tol === undefined ? 0.05 : tol); }

  var FS = 17;
  var LINES = [[100, '1.0'], [115, '1.15'], [125, '1.25'], [140, '1.4'], [150, ''], [175, '1.75'], [200, '2.0']];
  var PARAS = [0, 3, 6, 9, 12, 15, 18];
  var LISTS = [[0, '0.0em', 0], [10, '0.1em', 0.1], [25, '', 0.25], [40, '0.4em', 0.4], [60, '0.6em', 0.6]];
  function ptToPx(pt) { return pt * 4 / 3; }

  async function send(fields) {
    var next = ++seq;
    var done = new Promise(function (resolve) { waiters[next] = resolve; });
    window.__send(Object.assign({ seq: next }, fields));
    await done;
    await document.fonts.ready;
  }
  function spacing(lh, pspace, ligap) {
    window.__send({ type: 'spacing', lh: lh, pspace: pspace, ligap: ligap });
  }
  function reset() { spacing('', '', ''); }

  var LOREM = 'Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore ' +
    'et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip.';
  var HTML =
    '<p id="first">' + LOREM + '</p>' +
    '<h1 id="h1">Heading one</h1><p>' + LOREM + '</p>' +
    '<h2 id="h2">Heading two</h2><p>' + LOREM + '</p>' +
    '<h3 id="h3">Heading three</h3><p>' + LOREM + '</p>' +
    '<h4 id="h4">Heading four</h4><p>' + LOREM + '</p>' +
    '<h5 id="h5">Heading five</h5><p>' + LOREM + '</p>' +
    '<h6 id="h6">Heading six</h6><p id="p-after-h6">' + LOREM + '</p>' +
    '<p id="p-a">Paragraph A</p><p id="p-b">Paragraph B</p>' +
    '<ul id="tight"><li id="t1">one</li><li id="t2">two<ul id="nested"><li id="n1">n1</li><li id="n2">n2</li></ul></li><li id="t3">three</li></ul>' +
    '<ul id="loose"><li id="l1"><p>loose one</p></li><li id="l2"><p>loose two</p></li></ul>' +
    '<ul id="task" data-fastmd-task-list="1" data-fastmd-loose-list="1" class="contains-task-list">' +
      '<li class="task-list-item" data-fastmd-task-item="1" id="k1"><input type="checkbox"> one</li>' +
      '<li class="task-list-item" data-fastmd-task-item="1" id="k2"><input type="checkbox"> two</li></ul>' +
    '<ol id="ordered"><li>x</li><li>y</li></ol>' +
    '<dl id="dl"><dt>Term</dt><dd>Definition</dd></dl>' +
    '<h3 id="hpre">Before code</h3><pre id="pre"><code>line one\nline two\nline three</code></pre>' +
    '<h3 id="htab">Before table</h3><div class="table-wrap" id="tabwrap"><table id="table"><thead><tr><th>A</th><th>B</th></tr></thead>' +
      '<tbody><tr><td>' + LOREM + '</td><td>x</td></tr></tbody></table></div>' +
    '<h3 id="hq">Before quote</h3><blockquote id="quote"><p>Quoted</p></blockquote>' +
    '<p id="math">Inline \\(\\frac{a}{b}\\) and \\(\\sum_{i=1}^{n}\\frac{x_i}{y_i}\\) with <code>code</code>, <mark>mark</mark>, ' +
      '<kbd>K</kbd> and \\(x^2\\). ' + LOREM + ' Again \\(\\frac{1}{2}\\) and ' + LOREM + '</p>' +
    '<p>$$\\sum_{i=1}^{n}\\frac{x_i}{y_i}$$</p><p id="last">End</p>';

  function $(id) { return document.getElementById(id); }
  function cs(el) { return getComputedStyle(el); }
  function px(value) { return parseFloat(value); }
  function blocks() { return Array.prototype.slice.call(document.getElementById('content').children); }

  var HEADINGS = ['h1', 'h2', 'h3', 'h4', 'h5', 'h6'];
  // Everything the finalized heading typography, code blocks and math own.
  function fixedSnapshot() {
    var snap = {};
    HEADINGS.forEach(function (id) {
      var s = cs($(id));
      snap[id] = [s.marginTop, s.marginBottom, s.lineHeight, s.fontSize, s.fontWeight].join('|');
    });
    snap.pre = cs($('pre')).lineHeight + '|' + cs($('pre')).marginTop + '|' + cs($('pre')).marginBottom;
    snap.quote = cs($('quote')).marginTop + '|' + cs($('quote')).marginBottom;
    snap.tabwrap = cs($('tabwrap')).marginTop + '|' + cs($('tabwrap')).marginBottom;
    var display = document.querySelector('.katex-display');
    snap.display = display ? cs(display).marginTop + '|' + cs(display).marginBottom : 'none';
    var inline = document.querySelector('#math .katex');
    snap.katex = inline ? cs(inline).lineHeight + '|' + cs(inline).fontSize : 'none';
    return snap;
  }
  // The real vertical distance between a heading and the block after it.
  function gapBelow(id) {
    var h = $(id), next = h.nextElementSibling;
    return next.getBoundingClientRect().top - h.getBoundingClientRect().bottom;
  }
  function gapAbove(id) {
    var h = $(id), prev = h.previousElementSibling;
    return h.getBoundingClientRect().top - prev.getBoundingClientRect().bottom;
  }
  function geometry() {
    var g = {};
    HEADINGS.forEach(function (id) { g[id + 'Below'] = gapBelow(id); });
    ['hpre', 'htab', 'hq'].forEach(function (id) { g[id + 'Below'] = gapBelow(id); });
    return g;
  }

  try {
    window.__send({ type: 'theme', dark: false, fs: FS });
    await send({ type: 'content', html: HTML });
    await new Promise(function (r) { setTimeout(r, 400); });

    // -------------------------------------------------------------- defaults
    var base = fixedSnapshot();
    function clipDelta() {
      return [].slice.call(document.querySelectorAll('.table-wrap, .katex-display, pre, td, th'))
        .reduce(function (worst, el) { return Math.max(worst, el.scrollHeight - el.clientHeight); }, 0);
    }
    function boxHeight(sel) { return $('math').querySelector(sel).getBoundingClientRect().height; }
    var baseClip = clipDelta(), baseCode = boxHeight('code'), baseKbd = boxHeight('kbd');
    var baseGeometry = geometry();
    var rootStyle = document.documentElement.style;
    expect(!rootStyle.getPropertyValue('--lh') && !rootStyle.getPropertyValue('--pspace')
           && !rootStyle.getPropertyValue('--li-gap'), 'no spacing variable is set by default');
    expect(near(px(cs(document.body).lineHeight), 1.5 * FS), 'default body line-height is 1.5 (' + cs(document.body).lineHeight + ')');
    expect(near(px(cs($('p-a')).marginBottom), 16), 'default paragraph gap is 12pt = 16px');
    expect(near(px(cs($('tight')).marginBottom), 16) && near(px(cs($('ordered')).marginBottom), 16)
           && near(px(cs($('dl')).marginBottom), 16), 'default list and dl bottom gaps are 16px');
    expect(near(px(cs($('t2')).marginTop), 0.25 * FS), 'default tight item gap 0.25em');
    expect(near(px(cs($('l1').querySelector('p')).marginBottom), 0.5 * FS), 'default loose item gap 0.5em');
    expect(near(px(cs($('nested')).marginTop), 0.25 * FS) && near(px(cs($('nested')).marginBottom), 0.35 * FS, 0.1),
           'default nested list margins 0.25em / 0.35em');
    expect(near(px(cs($('k1')).marginBottom), 16), 'default loose task item gap 16px');
    expect(near(px(cs($('table')).lineHeight), 1.5 * px(cs($('table')).fontSize)), 'default table line-height 1.5');
    expect(near(px(cs($('pre')).lineHeight), 1.5 * px(cs($('pre')).fontSize)), 'code block keeps 1.5');
    expect(near(px(cs($('h1')).lineHeight), 1.25 * px(cs($('h1')).fontSize)), 'headings keep 1.25');
    expect(base.katex !== 'none' && base.display !== 'none', 'math was typeset (' + base.katex + ')');

    // Inline code and kbd are exactly as before at the default leading and above.
    [['1.5', ''], ['1.75', '1.75'], ['2.0', '2.0']].forEach(function (entry) {
      spacing(entry[1], '', '');
      expect(near(boxHeight('code'), baseCode, 0.05) && near(boxHeight('kbd'), baseKbd, 0.05),
             'line ' + entry[0] + ': inline code and kbd boxes unchanged');
    });
    reset();

    // ---------------------------------------------------------- line presets
    LINES.forEach(function (entry) {
      var value = entry[0] / 100;
      spacing(entry[1], '', '');
      expect(near(px(cs(document.body).lineHeight), value * FS, 0.1), 'line ' + value + ': body line-height');
      expect(near(px(cs($('p-a')).lineHeight), value * FS, 0.1), 'line ' + value + ': paragraphs inherit it');
      expect(near(px(cs($('t1')).lineHeight), value * FS, 0.1), 'line ' + value + ': list items inherit it');
      var tableFs = px(cs($('table')).fontSize);
      expect(near(px(cs($('table')).lineHeight), Math.min(value, 1.5) * tableFs, 0.1),
             'line ' + value + ': table capped at 1.5 (' + cs($('table')).lineHeight + ')');
      expect(JSON.stringify(fixedSnapshot()) === JSON.stringify(base),
             'line ' + value + ': headings, code, quote, table and math spacing unchanged');
      var g = geometry(), same = Object.keys(baseGeometry).every(function (k) { return near(g[k], baseGeometry[k], 0.6); });
      expect(same, 'line ' + value + ': heading-to-block gaps unchanged');
    });
    reset();

    // ------------------------------------------------------ paragraph presets
    PARAS.forEach(function (pt) {
      spacing('', pt === 12 ? '' : pt + 'pt', '');
      var want = ptToPx(pt);
      expect(near(px(cs($('p-a')).marginBottom), want), 'paragraph ' + pt + 'pt: p gap ' + want.toFixed(2) + 'px');
      expect(near(px(cs($('tight')).marginBottom), want) && near(px(cs($('dl')).marginBottom), want)
             && near(px(cs($('k1')).marginBottom), want), 'paragraph ' + pt + 'pt: lists, dl, loose tasks follow');
      expect(near(px(cs($('t2')).marginTop), 0.25 * FS), 'paragraph ' + pt + 'pt: list item gap independent');
      expect(near(px(cs(document.body).lineHeight), 1.5 * FS), 'paragraph ' + pt + 'pt: line height independent');
      expect(JSON.stringify(fixedSnapshot()) === JSON.stringify(base),
             'paragraph ' + pt + 'pt: heading margins and block-card margins unchanged');
      var gapAB = $('p-b').getBoundingClientRect().top - $('p-a').getBoundingClientRect().bottom;
      expect(near(gapAB, want, 0.6), 'paragraph ' + pt + 'pt: real gap between paragraphs');
      // Natural collapsing: a heading is never closer than the paragraph above.
      HEADINGS.forEach(function (id) {
        var mt = px(cs($(id)).marginTop);
        expect(near(gapAbove(id), Math.max(mt, want), 0.6),
               'paragraph ' + pt + 'pt: gap above ' + id + ' is max(heading margin, paragraph gap)');
      });
      var g = geometry(), same = Object.keys(baseGeometry).every(function (k) { return near(g[k], baseGeometry[k], 0.6); });
      expect(same, 'paragraph ' + pt + 'pt: heading-to-block gaps unchanged');
    });
    reset();

    // ----------------------------------------------------------- list presets
    LISTS.forEach(function (entry) {
      var em = entry[2] * FS;
      spacing('', '', entry[1]);
      expect(near(px(cs($('t2')).marginTop), em) && near(px(cs($('t2')).marginBottom), em),
             'list ' + entry[2] + 'em: tight item gap');
      expect(near(px(cs($('l1').querySelector('p')).marginBottom), 2 * em, 0.1),
             'list ' + entry[2] + 'em: loose item gap is twice it');
      expect(near(px(cs($('nested')).marginTop), em) && near(px(cs($('nested')).marginBottom), 1.4 * em, 0.1),
             'list ' + entry[2] + 'em: nested list keeps its 1 / 1.4 ratio');
      expect(near(px(cs($('p-a')).marginBottom), 16) && near(px(cs($('tight')).marginBottom), 16),
             'list ' + entry[2] + 'em: paragraph and list-end gaps independent');
      expect(JSON.stringify(fixedSnapshot()) === JSON.stringify(base), 'list ' + entry[2] + 'em: fixed spacing unchanged');
      var gap = $('t3').getBoundingClientRect().top - $('t2').getBoundingClientRect().bottom;
      expect(gap >= -0.5, 'list ' + entry[2] + 'em: items never overlap');
    });
    reset();

    // ---------------------------------------- zero spacing and line-height 1.0
    spacing('1.0', '0pt', '0.0em');
    expect(px(cs($('p-a')).marginBottom) === 0 && px(cs($('tight')).marginBottom) === 0, 'zero paragraph spacing: no margin');
    expect(near($('p-b').getBoundingClientRect().top, $('p-a').getBoundingClientRect().bottom, 0.5),
           'zero paragraph spacing: paragraphs touch, never overlap');
    expect(px(cs($('t1')).marginTop) === 0 && px(cs($('l1').querySelector('p')).marginBottom) === 0,
           'zero list spacing: item margins are 0');
    expect(near($('t2').getBoundingClientRect().top, $('t1').getBoundingClientRect().bottom, 0.5),
           'zero list spacing: items touch, never overlap');
    await new Promise(function (r) { setTimeout(r, 50); });
    var kids = blocks(), overlap = 0;
    for (var i = 0; i + 1 < kids.length; i++) {
      if (kids[i + 1].getBoundingClientRect().top < kids[i].getBoundingClientRect().bottom - 0.5) overlap++;
    }
    expect(overlap === 0, 'line-height 1.0 + zero spacing: no top-level block overlaps the next');
    var clipped = [].slice.call(document.querySelectorAll('.table-wrap, .katex-display, pre, td, th'))
      .filter(function (el) { return el.scrollHeight - el.clientHeight > baseClip + 1; });
    function describe(el) {
      var r = el.getBoundingClientRect();
      return el.tagName.toLowerCase() + (el.className ? '.' + String(el.className).split(' ').join('.') : '') +
        '[h=' + r.height.toFixed(1) + ' client=' + el.clientHeight + ' scroll=' + el.scrollHeight + ']';
    }
    expect(clipped.length === 0, 'line-height 1.0: nothing is clipped beyond what the default already is (base overflow ' + baseClip + 'px; ' + clipped.map(describe).join(', ') + ')');
    var mathP = $('math').getBoundingClientRect();
    var escaped = [].slice.call($('math').querySelectorAll('.katex .base, code, mark, kbd')).filter(function (el) {
      var r = el.getBoundingClientRect();
      // Code, mark and kbd paint a box taller than a 1.0 line (their padding and
      // the font's own ascent/descent), so allow the few px that bleed above the
      // first line; a formula's layout boxes must be fully inside.
      var slack = el.matches('code, mark, kbd') ? 3.5 : 0.5;
      return r.top < mathP.top - slack || r.bottom > mathP.bottom + slack;
    });
    expect(escaped.length === 0, 'line-height 1.0: formula boxes, code, mark and kbd stay inside their paragraph (' +
           escaped.map(function (el) {
             var r = el.getBoundingClientRect();
             return el.tagName.toLowerCase() + '.' + el.className + ' top ' + (r.top - mathP.top).toFixed(2) +
               ' bottom ' + (r.bottom - mathP.bottom).toFixed(2);
           }).join('; ') + ')');
    var inlineMath = [].slice.call($('math').querySelectorAll('.katex .base')), inlineOverlap = 0;
    var pitch = px(cs($('math')).lineHeight);
    expect(boxHeight('code') - pitch <= 2.5 && boxHeight('kbd') - pitch <= 5.5,
           'line-height 1.0: inline code/kbd boxes stay within a few px of the line pitch (code ' +
           boxHeight('code').toFixed(2) + ', kbd ' + boxHeight('kbd').toFixed(2) + ' vs ' + pitch + ')');
    for (var a = 0; a < inlineMath.length; a++) {
      for (var b = a + 1; b < inlineMath.length; b++) {
        var ra = inlineMath[a].getBoundingClientRect(), rb = inlineMath[b].getBoundingClientRect();
        if (ra.left < rb.right - 0.5 && rb.left < ra.right - 0.5 && ra.top < rb.bottom - 0.5 && rb.top < ra.bottom - 0.5)
          inlineOverlap++;
      }
    }
    expect(inlineOverlap === 0, 'line-height 1.0: formula layout boxes never overlap each other');
    expect(JSON.stringify(fixedSnapshot()) === JSON.stringify(base), 'minimum spacing: fixed spacing unchanged');
    expect(near(px(cs($('table')).lineHeight), 1.0 * px(cs($('table')).fontSize), 0.1), 'table follows 1.0 (the cap only limits upward)');
    reset();

    // ------------------------------------------- immediate updates and reset
    var before = $('p-a').getBoundingClientRect().height;
    spacing('2.0', '18pt', '0.6em');
    var after = $('p-a').getBoundingClientRect().height;
    expect(after > before, 'a spacing message restyles the live page at once (' + before.toFixed(1) + ' -> ' + after.toFixed(1) + ')');
    await send({ type: 'content', html: HTML.replace('Paragraph A', 'Paragraph A edited') });
    expect(near(px(cs($('p-a')).marginBottom), ptToPx(18)) && near(px(cs(document.body).lineHeight), 2 * FS, 0.1),
           'a later content push keeps the chosen spacing');
    window.__send({ type: 'theme', dark: true, fs: 14 });
    expect(near(px(cs($('p-a')).marginBottom), ptToPx(18)) && near(px(cs(document.body).lineHeight), 2 * 14, 0.1),
           'a theme and font-size message keeps the chosen spacing');
    window.__send({ type: 'theme', dark: false, fs: FS });
    reset();
    await send({ type: 'content', html: HTML });
    await new Promise(function (r) { setTimeout(r, 200); });
    expect(!rootStyle.getPropertyValue('--lh') && !rootStyle.getPropertyValue('--pspace')
           && !rootStyle.getPropertyValue('--li-gap'), 'resetting removes every spacing variable');
    expect(JSON.stringify(fixedSnapshot()) === JSON.stringify(base)
           && near(px(cs(document.body).lineHeight), 1.5 * FS) && near(px(cs($('p-a')).marginBottom), 16)
           && near(px(cs($('t2')).marginTop), 0.25 * FS), 'back at the defaults after a reset');

    // The standalone export/print page carries the same values as a :root rule
    // (DocumentSpacing::rootCss); it must resolve to exactly what the message does.
    function resolved() {
      var s = cs(document.body);
      return [s.lineHeight, cs($('p-a')).marginBottom, cs($('tight')).marginBottom, cs($('t2')).marginTop,
              cs($('l1').querySelector('p')).marginBottom, cs($('nested')).marginBottom, cs($('table')).lineHeight].join('|');
    }
    spacing('1.25', '6pt', '0.4em');
    var viaMessage = resolved();
    reset();
    var style = document.createElement('style');
    style.textContent = ':root{--lh:1.25;--pspace:6pt;--li-gap:0.4em;}';
    document.head.appendChild(style);
    var viaRule = resolved();
    style.remove();
    expect(viaMessage === viaRule, 'the exported :root rule and the live message resolve identically');
  } catch (e) {
    fail('exception: ' + (e && e.stack || e));
  }

  var out = document.createElement('pre');
  out.id = 'spacingtest-out';
  out.textContent = 'SPACINGTEST_BEGIN\n' + results.join('\n') + '\nSPACINGTEST_END\nSPACINGTEST_VERDICT ' +
    (failures ? failures + ' FAILED' : 'ALL PASSED') + '\n';
  document.documentElement.appendChild(out);
})();
