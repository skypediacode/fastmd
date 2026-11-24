/* FastMD's offline Markmap shell. The host posts Markdown after the editor's
 * normal debounce; the page keeps one Markmap SVG instance for its lifetime. */
(function () {
  'use strict';
  var host = window.chrome && window.chrome.webview;
  var svg = document.getElementById('mindmap');
  var map = null;
  var renderedOnce = false;

  function post(message) { if (host) host.postMessage(message); }

  // markmap-lib records each block's source range on payload.lines as the
  // *string* "start,end" (0-based, end-exclusive) that it renders into the
  // data-lines attribute -- not an array. It has already offset the range past
  // any YAML front matter, so the raw document we are handed needs no
  // correction of our own. The content scan is the last resort for a node whose
  // own payload carries no range but whose HTML embeds a block that does
  // (a fenced code block or table lifted into its own node).
  function sourceLine(node) {
    var lines = node && node.payload && node.payload.lines;
    if (Array.isArray(lines)) lines = lines[0];
    if (lines !== undefined && lines !== null) {
      var start = parseInt(String(lines).split(',')[0], 10);
      if (start >= 0) return start + 1;
    }
    var match = /data-lines\s*=\s*["'](\d+)/i.exec(node && node.content || '');
    return match ? Number(match[1]) + 1 : 0;
  }

  // Double-click matches the Markdown preview's own go-to-source gesture, and
  // leaves single clicks to folding and panning.
  //
  // Bound once, in the capture phase, on the SVG root: Markmap installs
  // `dblclick` -> stopPropagation on every node's foreignObject, so a listener
  // on the node group would never see a double-click on the node's own text.
  // Delegating also means there is nothing to re-bind after each render.
  svg.addEventListener('dblclick', function (event) {
    var element = event.target;
    while (element && element !== svg
           && !(element.classList && element.classList.contains('markmap-node')))
      element = element.parentNode;
    if (!element || element === svg) return;
    var line = sourceLine(element.__data__);
    if (line > 0) post({ type: 'mindMapSource', line: line });
  }, true);

  async function render(markdown, seq) {
    try {
      var transformer = new window.markmap.Transformer();
      var result = transformer.transform(markdown || '');
      if (!map) {
        map = window.markmap.Markmap.create(svg, {
          autoFit: true,
          duration: 180,
          // `pan` is Markmap's *wheel*-pan handler and `scrollForPan` narrows
          // its zoom filter to Ctrl+wheel; both are off because the capture
          // listener below owns every wheel event. Drag-to-pan comes from
          // `zoom`, not from `pan`.
          pan: false,
          scrollForPan: false,
          zoom: true
        }, result.root);
      } else {
        // Do not auto-fit after edits: users retain their current pan/zoom.
        await map.setData(result.root, { autoFit: !renderedOnce });
      }
      renderedOnce = true;
    } catch (error) {
      svg.replaceChildren();
    }
    post({ type: 'rendered', seq: seq });
  }

  function serializeSvg() {
    var width = svg.clientWidth || 1200;
    var height = svg.clientHeight || 800;
    var copy = svg.cloneNode(true);
    copy.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    copy.setAttribute('width', String(width));
    copy.setAttribute('height', String(height));
    copy.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
    // The exported SVG carries no stylesheet, so the live theme's custom
    // properties have to be resolved and inlined here.
    var theme = getComputedStyle(document.documentElement);
    var style = document.createElementNS('http://www.w3.org/2000/svg', 'style');
    style.textContent = ':root{'
      + '--markmap-text-color:' + theme.getPropertyValue('--mindmap-text')
      + ';--markmap-circle-open-bg:' + theme.getPropertyValue('--mindmap-bg')
      + ';--markmap-code-bg:' + theme.getPropertyValue('--markmap-code-bg')
      + ';--markmap-code-color:' + theme.getPropertyValue('--markmap-code-color') + ';}';
    copy.insertBefore(style, copy.firstChild);
    return new XMLSerializer().serializeToString(copy);
  }

  // A mind map is a canvas, not a document: there is no scroll axis for a bare
  // wheel to mean anything else, so it zooms exactly like Ctrl+wheel does.
  // Panning is the drag gesture (d3.zoom, enabled by the `zoom` option).
  //
  // This runs in the capture phase at window level, ahead of every listener
  // Markmap installs on the SVG, and stops propagation -- which is also why
  // WebView2 owning the wheel in its native child HWND does not matter here,
  // and why Markmap's own `pan`/`scrollForPan` wheel wiring stays off.
  window.addEventListener('wheel', function (event) {
    if (!map) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    var factor = Math.pow(2, -event.deltaY / 400);
    map.svg.call(map.zoom.scaleBy, factor, [event.clientX, event.clientY]);
  }, { capture: true, passive: false });

  if (host) host.addEventListener('message', function (event) {
    var message = event.data || {};
    if (message.type === 'content') render(message.markdown, message.seq);
    else if (message.type === 'theme') document.documentElement.setAttribute('data-theme', message.dark ? 'dark' : 'light');
    else if (message.type === 'mindMapSvg') post({ type: 'mindMapSvg', svg: serializeSvg() });
    else if (message.type === 'mindMapFit') { if (map) map.fit(); }
  });

  document.addEventListener('contextmenu', function (event) {
    event.preventDefault();
    post({ type: 'contextMenu', x: event.clientX, y: event.clientY, hasSelection: false });
  });

  post({ type: 'ready' });
}());
