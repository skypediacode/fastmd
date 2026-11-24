// Stands in for WebView2's chrome.webview bridge so terminal.js runs unmodified
// and every message it would send to the host is recorded instead.
(function () {
  var listeners = [];
  window.keytestPosted = [];
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    postMessage: function (message) { window.keytestPosted.push(message); },
    addEventListener: function (type, listener) {
      if (type === 'message') listeners.push(listener);
    }
  };
  window.keytestSendToPage = function (message) {
    listeners.forEach(function (listener) { listener({ data: message }); });
  };
})();
