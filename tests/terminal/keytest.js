// Drives real keydown events through xterm.js and terminal.js and checks the
// exact bytes the page hands the host for the ConPTY input pipe.
(function () {
  var results = [];
  var failures = 0;

  var ESC_DOWN = '\x1b[27;1;27;1;0;1_';
  var ESC_UP = '\x1b[27;1;27;0;0;1_';

  function check(condition, label, detail) {
    if (!condition) failures++;
    results.push((condition ? 'PASS ' : 'FAIL ') + label + (condition || !detail ? '' : '  -- ' + detail));
  }

  function show(text) {
    return JSON.stringify(text).replace(/\\u001b/g, '\\e');
  }

  function decode(base64) {
    var binary = atob(base64);
    var bytes = new Uint8Array(binary.length);
    for (var i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return new TextDecoder('utf-8').decode(bytes);
  }

  function output(id, text) {
    var bytes = new TextEncoder().encode(text);
    var binary = '';
    for (var i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    window.keytestSendToPage({ type: 'output', id: id, data: btoa(binary) });
  }

  // Returns everything the page posted synchronously while the key was
  // dispatched: nothing here awaits, so a later post would be missed, which is
  // exactly the "no buffering or delay" property under test.
  function press(id, init, keyCode) {
    var textarea = document.querySelector(
      '.terminal-host[data-session="' + id + '"] textarea.xterm-helper-textarea');
    window.keytestPosted.length = 0;
    var event = new KeyboardEvent('keydown', Object.assign(
      { bubbles: true, cancelable: true }, init));
    Object.defineProperty(event, 'keyCode', { get: function () { return keyCode; } });
    Object.defineProperty(event, 'which', { get: function () { return keyCode; } });
    textarea.dispatchEvent(event);
    var inputs = [];
    var others = [];
    window.keytestPosted.forEach(function (message) {
      if (message.type === 'input' && message.id === id) inputs.push(decode(message.data));
      else if (message.type !== 'resize') others.push(message.type);
    });
    return { inputs: inputs, others: others, prevented: event.defaultPrevented };
  }

  function expectInput(id, label, init, keyCode, expected) {
    var sent = press(id, init, keyCode);
    check(sent.inputs.length === 1 && sent.inputs[0] === expected, label,
      'expected ' + show([expected]) + ', got ' + show(sent.inputs));
    return sent;
  }

  function run() {
    var ESCAPE = { key: 'Escape', code: 'Escape' };

    // Session 1: a shell before ConPTY has negotiated Win32 input mode keeps
    // xterm's plain VT encoding.
    window.keytestSendToPage({ type: 'create', id: 1 });
    window.keytestSendToPage({ type: 'show', id: 1 });
    expectInput(1, 'legacy mode: bare Esc is a single raw 0x1B', ESCAPE, 27, '\x1b');

    // ConPTY requests Win32 input mode on startup; split the request across two
    // reads to cover the chunk-boundary detection too.
    output(1, 'prompt\x1b[?90');
    output(1, '01h');

    var esc = expectInput(1, 'win32 mode: bare Esc is one KEY_EVENT_RECORD down/up pair',
      ESCAPE, 27, ESC_DOWN + ESC_UP);
    check(esc.prevented, 'win32 mode: Esc default is prevented, so xterm adds no second ESC');
    check(esc.others.length === 0, 'win32 mode: Esc triggers no copy/paste/other host message',
      show(esc.others));
    for (var repeat = 0; repeat < 3; repeat++) {
      expectInput(1, 'win32 mode: repeated Esc #' + (repeat + 2) + ' is emitted immediately',
        ESCAPE, 27, ESC_DOWN + ESC_UP);
    }
    expectInput(1, 'win32 mode: Shift+Esc carries SHIFT_PRESSED',
      Object.assign({ shiftKey: true }, ESCAPE), 27,
      '\x1b[27;1;27;1;16;1_\x1b[27;1;27;0;16;1_');

    var composing = press(1, Object.assign({ isComposing: true }, ESCAPE), 229);
    check(composing.inputs.every(function (data) { return data.indexOf('[27;1;27') < 0; }),
      'win32 mode: Esc during IME composition is left to xterm', show(composing.inputs));

    expectInput(1, 'Alt+Esc is still xterm\'s ESC ESC',
      Object.assign({ altKey: true }, ESCAPE), 27, '\x1b\x1b');
    expectInput(1, 'Alt+k is still ESC k', { key: 'k', code: 'KeyK', altKey: true }, 75, '\x1bk');
    expectInput(1, 'Alt+. is still ESC .', { key: '.', code: 'Period', altKey: true }, 190, '\x1b.');
    expectInput(1, 'Ctrl+[ is still a raw 0x1B',
      { key: '[', code: 'BracketLeft', ctrlKey: true }, 219, '\x1b');
    expectInput(1, 'Ctrl+C without a selection is still ETX', { key: 'c', code: 'KeyC', ctrlKey: true },
      67, '\x03');
    expectInput(1, 'Up arrow is unchanged', { key: 'ArrowUp', code: 'ArrowUp' }, 38, '\x1b[A');
    expectInput(1, 'F5 is unchanged', { key: 'F5', code: 'F5' }, 116, '\x1b[15~');
    expectInput(1, 'Shift+Enter still sends Win32 Shift/Enter records',
      { key: 'Enter', code: 'Enter', shiftKey: true }, 13,
      '\x1b[16;42;0;1;16;1_\x1b[13;28;13;1;16;1_\x1b[13;28;13;0;16;1_\x1b[16;42;0;0;0;1_');
    expectInput(1, 'Ctrl+Backspace still sends Win32 Ctrl/Backspace records',
      { key: 'Backspace', code: 'Backspace', ctrlKey: true }, 8,
      '\x1b[17;29;0;1;8;1_\x1b[8;14;0;1;8;1_\x1b[8;14;0;0;8;1_\x1b[17;29;0;0;0;1_');

    var paste = press(1, { key: 'v', code: 'KeyV', ctrlKey: true }, 86);
    check(paste.inputs.length === 0 && paste.others.join() === 'requestPaste',
      'Ctrl+V still requests a host paste', show(paste));

    // Session 2: a Kitty-protocol peer (agy) under ConPTY still gets an
    // unambiguous Escape; ConPTY delivers it to a VT-input reader as 0x1B.
    window.keytestSendToPage({ type: 'create', id: 2 });
    window.keytestSendToPage({ type: 'show', id: 2 });
    output(2, '\x1b[?9001h\x1b[?u\x1b[>1u');
    expectInput(2, 'kitty peer: bare Esc is one KEY_EVENT_RECORD down/up pair',
      ESCAPE, 27, ESC_DOWN + ESC_UP);
    expectInput(2, 'kitty peer: Shift+Enter still uses CSI u',
      { key: 'Enter', code: 'Enter', shiftKey: true }, 13, '\x1b[13;2u');
  }

  try {
    run();
  } catch (error) {
    failures++;
    results.push('FAIL harness threw: ' + (error && error.stack || error));
  }
  results.push(failures === 0 ? 'KEYTEST_VERDICT ALL PASSED'
                              : 'KEYTEST_VERDICT ' + failures + ' FAILED');
  document.getElementById('result').textContent =
    'KEYTEST_BEGIN\n' + results.join('\n') + '\nKEYTEST_END';
})();
