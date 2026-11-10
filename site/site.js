// LayerTack's page: the shortcut list and the year
(function () {
  'use strict';
  const keys = [
    ['V', 'Move'], ['M', 'Marquee'], ['L', 'Lasso'], ['W', 'Magic Wand'], ['B', 'Brush'], ['E', 'Eraser'],
    ['G', 'Paint Bucket'], ['I', 'Eyedropper'], ['H', 'Hand'], ['[ ]', 'brush size'], ['Shift+[ ]', 'hardness'],
    ['X', 'swap colours'], ['Ctrl+T', 'Free Transform'], ['Ctrl+J', 'duplicate'], ['Ctrl+E', 'merge down'],
    ['Ctrl+M', 'Curves'], ['Ctrl+L', 'Levels'], ['Ctrl+U', 'Hue/Saturation'], ['Ctrl+Shift+I', 'inverse selection'],
    ['Ctrl+Z', 'undo']
  ];
  const el = document.getElementById('keys');
  if (el) {
    for (const [k, what] of keys) {
      const kbd = document.createElement('kbd');
      kbd.textContent = k;
      el.appendChild(kbd);
      el.appendChild(document.createTextNode(what + ' '));
    }
  }
  const year = document.getElementById('year');
  if (year) year.textContent = String(Math.max(2026, new Date().getFullYear()));
})();
