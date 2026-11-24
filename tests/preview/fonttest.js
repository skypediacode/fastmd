// The 'font' message: body font-family, the @font-face rules it carries, and
// that the Vietnamese fallback really replaces letters Georgia would build
// from its own base letter and a loosely placed mark. FontPreferencesTest
// checks the C++ side of these strings; this mirrors them.
(async function () {
  var results = [], failures = 0;
  function pass(name) { results.push('PASS  ' + name); }
  function fail(name) { failures++; results.push('FAIL  ' + name); }
  function expect(ok, name) { ok ? pass(name) : fail(name); }

  var STACK = "'Segoe UI','SF Pro Text',system-ui,-apple-system,'Helvetica Neue',Arial,sans-serif";
  var RANGE = 'U+01A0-01A1,U+01AF-01B0,U+1EA0-1EF9';
  var FACES = [['Segoe UI', 400, 'normal'], ['Segoe UI Bold', 700, 'normal'],
               ['Segoe UI Italic', 400, 'italic'], ['Segoe UI Bold Italic', 700, 'italic']]
    .map(function (f) {
      return '@font-face{font-family:"FastMD Vietnamese";src:local("' + f[0] + '");font-weight:' + f[1] +
        ';font-style:' + f[2] + ';unicode-range:' + RANGE + ';}';
    }).join('');
  var GEORGIA = '"Georgia",' + STACK;
  var GUARDED = '"FastMD Vietnamese",' + GEORGIA;

  function font(family, faces) {
    var msg = { type: 'font', family: family };
    if (faces !== undefined) msg.faces = faces;
    window.__send(msg);
  }
  function faceStyles() { return document.querySelectorAll('style#fastmd-font-faces'); }

  var probe = document.createElement('div');
  probe.style.cssText = 'position:absolute;left:0;top:0;white-space:nowrap;font-size:48px';
  document.body.appendChild(probe);
  function width(text, family, weight) {
    probe.style.fontFamily = family || '';
    probe.style.fontWeight = weight || '400';
    probe.textContent = text;
    return probe.getBoundingClientRect().width;
  }
  function bodyWidth(text, weight) { return width(text, '', weight); }

  try {
    font(GUARDED, FACES);
    var styles = faceStyles();
    expect(styles.length === 1 && styles[0].textContent === FACES, 'faces installed in one <style>');
    var applied = document.body.style.fontFamily;
    expect(applied.indexOf('FastMD Vietnamese') >= 0 && applied.indexOf('FastMD Vietnamese') < applied.indexOf('Georgia'),
           'fallback face leads the body stack: ' + applied);
    font(GUARDED, FACES);
    expect(faceStyles().length === 1, 'a repeated message adds no second <style>');

    await document.fonts.load('400 48px "FastMD Vietnamese"', 'ế');
    await document.fonts.load('700 48px "FastMD Vietnamese"', 'ế');
    // Inherit the body's family: the probe has no family of its own.
    var guarded = bodyWidth('ế'), guardedBold = bodyWidth('ế', '700');
    var segoe = width('ế', "'Segoe UI'"), segoeBold = width('ế', "'Segoe UI'", '700');
    expect(Math.abs(guarded - segoe) < 0.5, 'guarded ế is Segoe UI\'s (' + guarded.toFixed(2) + ' vs ' + segoe.toFixed(2) + ')');
    expect(Math.abs(guardedBold - segoeBold) < 0.5, 'guarded bold ế is Segoe UI Bold\'s');
    expect(Math.abs(bodyWidth('e') - width('e', "'Georgia'")) < 0.5, 'other letters stay Georgia');

    font(GEORGIA, '');
    expect(faceStyles().length === 0, 'a message without faces removes them');
    var unguarded = bodyWidth('ế');
    var composed = width('ê', "'Georgia'");
    expect(Math.abs(unguarded - segoe) >= 0.5 && unguarded > composed + 0.5,
           'unguarded Georgia builds ế from ê plus a spacing mark (' + unguarded.toFixed(2) +
           ' > ê ' + composed.toFixed(2) + ')');

    font(GUARDED, FACES);
    font('', '');
    expect(faceStyles().length === 0 && document.body.style.fontFamily === '', 'Default clears faces and family');
    font(GEORGIA);
    expect(faceStyles().length === 0 && document.body.style.fontFamily.indexOf('Georgia') >= 0,
           'a message from an older host (no faces) still sets the family');
  } catch (e) {
    fail(String(e.stack || e));
  }
  probe.remove();
  var box = document.createElement('pre');
  box.textContent = 'FONTTEST_BEGIN\n' + results.join('\n') +
    '\nFONTTEST_VERDICT ' + (failures ? failures + ' FAILED' : 'ALL PASSED') +
    '\nFONTTEST_END';
  document.body.appendChild(box);
})();
