// Exercise the real xterm mouse reporting and FastMD clipboard round trip.
(async function () {
  var results = [], failures = 0;
  function check(ok, label) {
    if (!ok) failures++;
    results.push((ok ? 'PASS ' : 'FAIL ') + label);
  }
  function wait() { return new Promise(function (resolve) { setTimeout(resolve, 50); }); }
  function send(message) { window.keytestSendToPage(message); }
  function output(text) { send({ type: 'output', id: 1, data: btoa(text) }); }
  function messages(type) {
    return window.keytestPosted.filter(function (m) { return m.type === type; });
  }
  function input() { return messages('input').map(function (m) { return atob(m.data); }).join(''); }
  var screen;
  function mouse(type, button, buttons, extra) {
    var rect = screen.getBoundingClientRect();
    screen.dispatchEvent(new MouseEvent(type, Object.assign({
      bubbles: true, cancelable: true, view: window,
      clientX: rect.left + 20, clientY: rect.top + 10, button: button, buttons: buttons,
      detail: type === 'mousemove' ? 0 : 1
    }, extra)));
  }
  function rightClick() {
    window.keytestPosted.length = 0;
    mouse('mousedown', 2, 2);
    mouse('mousemove', 2, 2, { movementX: 1 });
    // Windows sends contextmenu before the right-button release.
    mouse('contextmenu', 2, 2);
    mouse('mouseup', 2, 0);
  }
  try {
    send({ type: 'create', id: 1 });
    send({ type: 'show', id: 1 });
    screen = document.querySelector('.xterm-screen');
    output('selectable text\r\n');
    await wait();
    // Cover the prompt, console-input TUIs and VT-input TUIs. Mouse reporting
    // depends on console/application modes, not on a shell or executable name.
    for (var mode of [0, 1000, 1002, 1003]) {
      output('\x1b[?1006h' + (mode ? '\x1b[?' + mode + 'h' : ''));
      await wait();
      for (var bracketed of [false, true]) {
        output('\x1b[?2004' + (bracketed ? 'h' : 'l'));
        await wait();
        var label = 'mouse mode ' + mode + ', bracketed paste ' + bracketed + ': ';
        rightClick();
        check(messages('requestPaste').length === 1, label + 'one clipboard request');
        check(input() === '', label + 'no application mouse input from paste gesture');
        send({ type: 'paste', id: 1, text: 'abc' });
        check(input() === (bracketed ? '\x1b[200~abc\x1b[201~' : 'abc'),
          label + 'clipboard is emitted exactly once through xterm');
        for (var key of [{ key: 'v', ctrlKey: true }, { key: 'Insert', shiftKey: true }]) {
          window.keytestPosted.length = 0;
          var event = new KeyboardEvent('keydown', Object.assign({
            bubbles: true, cancelable: true
          }, key));
          document.querySelector('textarea.xterm-helper-textarea').dispatchEvent(event);
          check(event.defaultPrevented && messages('requestPaste').length === 1 && input() === '',
            label + key.key + ' requests exactly one paste and prevents browser default');
          send({ type: 'paste', id: 1, text: 'abc' });
          check(input() === (bracketed ? '\x1b[200~abc\x1b[201~' : 'abc'),
            label + key.key + ' emits clipboard once');
        }
      }
      if (mode) {
        window.keytestPosted.length = 0;
        mouse('mousedown', 0, 1);
        mouse('mouseup', 0, 0);
        check(input().indexOf('\x1b[<0;') === 0 && messages('requestPaste').length === 0,
          'mouse mode ' + mode + ': left click still reports to application');
        window.keytestPosted.length = 0;
        mouse('mousedown', 1, 4);
        mouse('mouseup', 1, 0);
        check(input().indexOf('\x1b[<1;') === 0 && messages('requestPaste').length === 0,
          'mouse mode ' + mode + ': middle click still reports to application');
        window.keytestPosted.length = 0;
        var rect = screen.getBoundingClientRect();
        screen.dispatchEvent(new WheelEvent('wheel', {
          bubbles: true, cancelable: true, clientX: rect.left + 20,
          clientY: rect.top + 10, deltaY: 100
        }));
        check(input().indexOf('\x1b[<65;') === 0,
          'mouse mode ' + mode + ': wheel still reports to application');
        mouse('mousedown', 0, 1, { shiftKey: true });
        mouse('mousemove', 0, 1, {
          shiftKey: true, clientX: screen.getBoundingClientRect().left + 110
        });
        mouse('mouseup', 0, 0, { shiftKey: true });
        rightClick();
        check(messages('copy').length === 1 && messages('requestPaste').length === 0
          && input() === '', 'mouse mode ' + mode + ': Shift-drag selection still copies');
        output('\x1b[?' + mode + 'l');
        await wait();
      }
    }
    // Selection must survive the press until contextmenu copies it.
    mouse('mousedown', 0, 1);
    mouse('mousemove', 0, 1, { clientX: screen.getBoundingClientRect().left + 110 });
    mouse('mouseup', 0, 0);
    rightClick();
    check(messages('copy').length === 1 && messages('copy')[0].text.length > 0
      && messages('requestPaste').length === 0 && input() === '',
      'right-click copies selection without pasting or reporting mouse input');
    rightClick();
    check(messages('requestPaste').length === 1, 'copy clears selection; next right-click pastes');
  } catch (error) {
    failures++;
    results.push('FAIL harness threw: ' + (error.stack || error));
  }
  document.getElementById('result').textContent = 'PASTETEST_BEGIN\n' + results.join('\n')
    + '\nPASTETEST_VERDICT ' + (failures ? failures + ' FAILED' : 'ALL PASSED') + '\nPASTETEST_END';
})();
