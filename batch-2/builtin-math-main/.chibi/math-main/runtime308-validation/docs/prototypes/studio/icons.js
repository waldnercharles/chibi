/* Icon names map to glyphs in third_party/fonts/nerd-fonts-symbols-only, the same
   font the native editor loads. RmlUi renders these as text in the icon family. */
'use strict';
window.ICONS = {
  add: 0xea60, remove: 0xeb3b, close: 0xea76, check: 0xeab2, search: 0xea6d, filter: 0xeaf1,
  down: 0xeab4, right: 0xeab6, left: 0xeab5, up: 0xeab7, caret: 0xf035d, caretRight: 0xf035f,
  more: 0xea7c, moreV: 0xeb10, gear: 0xeaf8, grip: 0xeb04,
  play: 0xf040a, pause: 0xf03e4, stop: 0xf04db, step: 0xf04ad,
  eye: 0xea70, eyeOff: 0xeae7, lock: 0xea75, unlock: 0xeb74, pin: 0xeb2b,
  scene: 0xebd2, graph: 0xebb9, prototype: 0xf01a7, behavior: 0xebb9, event: 0xea86,
  folder: 0xea83, folderOpen: 0xeaf7, file: 0xea7b, json: 0xeb0f, library: 0xea8b, material: 0xf03d8,
  outliner: 0xeb86, properties: 0xf062e, viewport: 0xf0d49, assets: 0xeb29, output: 0xeb9d,
  object: 0xf01a7, physics: 0xf0e7b, gameplay: 0xf04e5, render: 0xeac6, sceneSettings: 0xeaf8,
  select: 0xf01bf, move: 0xf01be, rotate: 0xf0467, scale: 0xf0a68, frame: 0xf004c, focus: 0xf01a3, pan: 0xf0bb4,
  zoomIn: 0xeb81, zoomOut: 0xeb82, grid: 0xf02c1, magnet: 0xf0347, overlay: 0xeb2f,
  mouse: 0xf037d, keyboard: 0xf030c, undo: 0xf054c, redo: 0xf044e, save: 0xeb4b, history: 0xea82,
  link: 0xeb15, external: 0xeb14, reset: 0xeae2, copy: 0xebcc, edit: 0xea73, trash: 0xea81,
  warning: 0xea6c, error: 0xea87, info: 0xea74, dot: 0xea71, target: 0xebf8, eyedropper: 0xf020a,
  split: 0xeb56, maximize: 0xeb4c, restore: 0xeb4d, layout: 0xebeb,
  fn: 0xf0871, branch: 0xf062c, expression: 0xf0169, variable: 0xea88, field: 0xeb5f,
  triangle: 0xf0537, hexagon: 0xf02d9, cloud: 0xf0163, water: 0xf0e0a, cube: 0xf01a7,
  camera: 0xf0d5d, light: 0xf0336, diamond: 0xf0b8b, circle: 0xf0766, system: 0xeba2, spray: 0xf0665,
  command: 0xea85, sparkle: 0xec10,
};
window.icon = (name, cls = '') =>
  `<span class="icon ${cls}" aria-hidden="true">${String.fromCodePoint(window.ICONS[name] ?? window.ICONS.cube)}</span>`;
