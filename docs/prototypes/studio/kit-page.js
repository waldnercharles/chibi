/* Component gallery: live demos plus forced states for each control. */
'use strict';
(() => {
const { $, h } = Kit;
const demo = (parent, cap, el) => { const d = h(`<div class="demo"><span class="cap">${cap}</span></div>`); d.appendChild(el); (typeof parent === 'string' ? $(parent) : parent).appendChild(d); return el; };
const box = (cls = '') => h(`<div class="${cls}"></div>`);
const report = (t) => console.log(t);
Kit.report = report;

// Tokens
const css = getComputedStyle(document.documentElement);
const tokenGroups = {
  Surfaces: ['--app', '--area', '--header', '--panel', '--panel-head', '--field', '--field-hover', '--input', '--popover', '--viewport'],
  Text: ['--fg', '--fg-2', '--fg-3', '--fg-off'],
  Roles: ['--accent', '--active-row', '--selected', '--ring', '--active-object', '--selected-object', '--override', '--danger', '--warning', '--success'],
  Types: ['--t-exec', '--t-float', '--t-int', '--t-bool', '--t-vec', '--t-color', '--t-string', '--t-entity'],
  Nodes: ['--n-event', '--n-flow', '--n-expr', '--n-component', '--n-call', '--n-spawn'],
  Metrics: ['--h', '--h-sm', '--row', '--gap', '--header-h', '--r', '--r-lg', '--label-w', '--press-ms', '--pop-ms', '--drawer-in', '--drawer-out'],
};
for (const [name, list] of Object.entries(tokenGroups)) {
  const t = h(`<div class="demo"><span class="cap">${name}</span><table class="tokens">${list.map((v) => { const val = css.getPropertyValue(v).trim(); return `<tr><td>${v}</td><td>${name === 'Metrics' ? '' : `<span class="chip" style="background:${val}"></span> `}${val}</td></tr>`; }).join('')}</table></div>`);
  $('#token-tables').appendChild(t);
}
// Icons
const ig = $('#icon-grid');
for (const [name, cp] of Object.entries(ICONS)) ig.appendChild(h(`<span class="btn subtle" data-tip="${name} · U+${cp.toString(16).toUpperCase()}" style="width:30px;padding:0">${icon(name)}</span>`));

// Buttons
const b = $('#demo-buttons');
[['Ghost', 'btn'], ['Subtle', 'btn subtle'], ['Primary', 'btn primary'], ['Outline', 'btn outline'], ['Danger', 'btn outline danger']].forEach(([l, c]) => demo(b, l, h(`<button class="${c}">${icon('save')}<span>Save</span></button>`)));
demo(b, 'Small', h(`<button class="btn subtle sm">${icon('add', 'sm')}<span>Add</span></button>`));
demo(b, 'Icon', h(`<button class="btn icon-only subtle" data-tip="Frame selected" data-kbd="F">${icon('frame')}</button>`));
demo(b, 'On', h(`<button class="btn icon-only" data-state="on" data-tip="Snap">${icon('magnet')}</button>`));
demo(b, 'Disabled', h(`<button class="btn subtle" data-disabled>${icon('save')}<span>Save</span></button>`));

// Toggle group
let shape = 1, mode = 'solid';
demo('#demo-toggle', 'Enum (3)', Kit.toggleGroup(box('w240'), { options: [{ value: 0, label: 'Static' }, { value: 1, label: 'Dynamic' }, { value: 2, label: 'Kinematic' }], value: () => shape, onChange: (v) => (shape = v) }).el);
demo('#demo-toggle', 'Icons', Kit.toggleGroup(box(), { options: [{ value: 'solid', icon: 'circle', tip: 'Solid' }, { value: 'rendered', icon: 'light', tip: 'Rendered' }], value: () => mode, onChange: (v) => (mode = v) }).el);

// Number fields
const nf = (cap, o, attrs = {}) => { const el = box('w200'); const f = Kit.numberField(el, { value: 120, step: 1, precision: 2, onCommit: (v) => report(`commit ${v}`), ...o }); Object.entries(attrs).forEach(([k, v]) => el.setAttribute(k, v)); demo('#demo-number', cap, el); return f; };
nf('Interactive', { value: 120, unit: 'u/s' });
nf('Integer', { value: 20, integer: true, precision: 0, step: 1 });
nf('Degrees', { value: 45, unit: '°', precision: 1 });
nf('Overridden', { value: -320, overridden: () => true });
nf('Hover', { value: 800, unit: 'u/s²' }, { style: 'background:var(--field-hover)' });
nf('Scrubbing', { value: 18.4 }, { 'data-scrubbing': '' });
demo('#demo-number', 'Editing', h('<div class="field w200" data-editing><input class="part value" value="0.125*2" style="text-align:right"><span class="part unit">s</span></div>'));
nf('Invalid', { value: 12 }, { 'data-invalid': '' });
nf('Read-only (playing)', { value: -188.54, readOnly: () => true }, { 'data-readonly': '' });
nf('Disabled', { value: 1 }, { 'data-disabled': '' });

// Vector
const vec = (cap, ov) => {
  const j = h('<div class="joined w240"></div>');
  ['x', 'y'].forEach((a, i) => { const f = box(); j.appendChild(f); Kit.numberField(f, { value: [-320, 0][i], addon: a.toUpperCase(), addonClass: a, overridden: () => ov && i === 0 }); });
  demo('#demo-vector', cap, j);
};
vec('Position', false);
vec('X overridden', true);

// Slider
demo('#demo-slider', 'Factor', (() => { const e = box('w200'); Kit.numberField(e, { value: 0.5, min: 0, max: 1, slider: true, step: 0.01, precision: 3 }); return e; })());
demo('#demo-slider', 'Intensity', (() => { const e = box('w200'); Kit.numberField(e, { value: 0.1, min: 0, max: 4, slider: true, step: 0.01, precision: 2 }); return e; })());
demo('#demo-slider', 'Full', (() => { const e = box('w200'); Kit.numberField(e, { value: 1, min: 0, max: 1, slider: true, step: 0.01, precision: 3 }); return e; })());

// Select
let sh = 4;
demo('#demo-select', 'Select', Kit.select(box('w200'), { options: ['Box', 'Circle', 'Diamond', 'Chevron', 'Stroked chevron', 'Capsule', 'Triangle', 'Stroked triangle'].map((label, value) => ({ value, label })), value: () => sh, onChange: (v) => (sh = v) }).el);
let tgt = 'scene';
demo('#demo-select', 'With icons', Kit.select(box('w200'), { options: [{ value: 'scene', label: 'Scene · Sandbox', icon: 'scene' }, { value: 'p', label: 'Prototype · Player', icon: 'triangle' }], value: () => tgt, onChange: (v) => (tgt = v) }).el);
demo('#demo-select', 'Open', (() => { const e = Kit.select(box('w200'), { options: [{ value: 0, label: 'Circle' }], value: () => 0, onChange: () => {} }).el; e.setAttribute('data-open', ''); return e; })());

// Checkbox / switch
let c1 = true, c2 = false;
demo('#demo-check', 'Checked', Kit.checkbox(h('<span></span>'), { label: 'Continuous', value: () => c1, onChange: (v) => (c1 = v) }).el);
demo('#demo-check', 'Unchecked', Kit.checkbox(h('<span></span>'), { label: 'Sensor', value: () => c2, onChange: (v) => (c2 = v) }).el);
demo('#demo-check', 'Disabled', (() => { const e = Kit.checkbox(h('<span></span>'), { label: 'Sleep', value: () => true, onChange: () => {} }).el; e.setAttribute('data-disabled', ''); return e; })());
{ const s = h('<span class="switch" data-checked role="switch" tabindex="0"><span class="thumb"></span></span>'); s.addEventListener('click', () => s.toggleAttribute('data-checked')); demo('#demo-check', 'Switch', s); }

// Color
let col = { r: 168, g: 68, b: 255, a: 1 };
demo('#demo-color', 'Color', Kit.colorField(box('w200'), { value: () => col, onChange: (c) => (col = c) }).el);
demo('#demo-color', 'None', Kit.colorField(box('w200'), { value: () => ({ r: 0, g: 0, b: 0, a: 0 }), onChange: () => {} }).el);
demo('#demo-color', 'Overridden', Kit.colorField(box('w200'), { value: () => ({ r: 255, g: 165, b: 58, a: 1 }), overridden: () => true, onChange: () => {} }).el);

// Text / search
demo('#demo-text', 'Text', h('<div class="field text left w200"><input class="part value" value="Cave guardian 1"></div>'));
demo('#demo-text', 'Editing', h('<div class="field text left w200" data-editing><input class="part value" value="Cave guardian"></div>'));
demo('#demo-text', 'Search', h(`<div class="field search w240"><span class="part addon">${icon('search', 'sm')}</span><input class="part value" placeholder="Search properties"></div>`));

// Property rows
{
  const s = h('<div class="surface w340"></div>');
  const row = (label, el, attrs = '') => { const r = h(`<div class="prop" ${attrs}><span class="label">${label}</span><div class="control"></div></div>`); r.lastChild.appendChild(el); s.appendChild(r); };
  const n = (o) => { const e = box(); Kit.numberField(e, { value: 0, ...o }); return e; };
  row('Speed', n({ value: 120, unit: 'u/s' }));
  row('Acceleration', n({ value: 800, unit: 'u/s²' }));
  row('Antialias', n({ value: 1, min: 0, max: 1, slider: true, precision: 3, step: 0.01 }));
  row('<span class="hl">Rad</span>ius', n({ value: 10 }), 'data-match');
  row('Sensor', Kit.checkbox(h('<span></span>'), { value: () => false, onChange: () => {} }).el);
  demo('#demo-prop', 'Rows', s);
}

// Panel
{
  const s = h('<div class="w340"></div>');
  for (const [title, open, badge] of [['Transform', true, 2], ['Appearance', false, 0], ['Movement', false, 0]]) {
    const p = h(`<section class="panel" ${open ? 'data-open' : ''}><header class="panel-head">${icon('down', 'chev')}<span class="title">${title}</span>${badge ? `<span class="badge override">${badge}</span>` : ''}<span class="spacer"></span><span class="grip">${icon('grip')}</span><button class="btn sm icon-only" data-stop>${icon('more')}</button></header><div class="panel-body"></div></section>`);
    const r = h('<div class="prop"><span class="label">Position</span><div class="control"><div class="joined"></div></div></div>');
    ['x', 'y'].forEach((a, i) => { const f = box(); r.querySelector('.joined').appendChild(f); Kit.numberField(f, { value: [-320, 0][i], addon: a.toUpperCase(), addonClass: a, overridden: () => true }); });
    p.lastChild.appendChild(r);
    Kit.panel(p);
    s.appendChild(p);
  }
  demo('#demo-panel', 'Panels', s);
}

// Tree rows
{
  const s = h('<div class="surface w300" style="padding:4px"></div>');
  const rows = [['Sandbox', 'scene', 0, 'root'], ['Player', 'triangle', 1, 'active', 4], ['Cave guardian 1', 'triangle', 1, 'selected', 3], ['Reflective crystal', 'diamond', 1, ''], ['Entry cloud', 'cloud', 1, 'hidden']];
  s.innerHTML = rows.map(([n, ic, d, st, beh]) => `<div class="tree-row" ${st === 'active' ? 'data-active data-selected' : st === 'selected' ? 'data-selected' : ''} style="padding-left:${6 + d * 14}px;${st === 'hidden' ? 'opacity:.5' : ''}"><span class="twisty">${st === 'root' ? icon('down', 'sm') : ''}</span>${icon(ic)}<span class="name">${n}</span><span class="inline-icons">${beh ? `${icon('graph')}<span class="meta">${beh}</span>` : ''}</span>${st === 'root' ? '<span class="meta">20</span>' : `<button class="toggle" ${st === 'hidden' ? 'data-off' : ''}>${icon(st === 'hidden' ? 'eyeOff' : 'eye')}</button><button class="toggle" data-off>${icon('unlock')}</button>`}</div>`).join('');
  demo('#demo-tree', 'Outliner', s);
}

// Context tabs
{
  const t = h(`<nav class="prop-tabs" style="height:auto;border-radius:6px">${[['sceneSettings'], ['object', 'active'], ['physics', 'match'], ['material', 'dim'], ['gameplay'], ['graph']].map(([ic, st]) => `<button class="tab" ${st === 'active' ? 'data-active' : ''} ${st === 'dim' ? 'data-dim' : ''}>${icon(ic)}${st === 'match' ? '<span class="match-dot"></span>' : ''}</button>`).join('')}</nav>`);
  demo('#demo-tabs', 'Active · match · dimmed', t);
}

// Area header
{
  const a = h(`<div class="area" style="width:720px;height:auto"><header class="area-head"><button class="btn editor-type">${icon('viewport')}${icon('down', 'caret')}</button><button class="btn menu-btn">View</button><button class="btn menu-btn">Select</button><button class="btn menu-btn">Add</button><span class="spacer"></span><span class="seg-demo"></span><button class="btn icon-only">${icon('overlay')}</button><button class="btn icon-only">${icon('magnet')}</button><button class="btn icon-only">${icon('maximize')}</button></header></div>`);
  let m = 'solid';
  Kit.toggleGroup(a.querySelector('.seg-demo'), { options: [{ value: 'solid', icon: 'circle' }, { value: 'rendered', icon: 'light' }], value: () => m, onChange: (v) => (m = v) });
  demo('#demo-area', 'Scene header', a);
  const g = h(`<div class="area" style="width:720px;height:auto"><header class="area-head"><button class="btn editor-type">${icon('graph')}${icon('down', 'caret')}</button><button class="btn menu-btn">View</button><button class="btn menu-btn">Add</button><div class="crumbs"><span class="row" style="gap:4px">${icon('triangle')}<span>Player</span></span><span class="crumb-sep">›</span><button class="btn">${icon('graph')}<span class="current">Movement</span>${icon('down', 'sm')}</button><span class="crumb-sep">›</span><button class="btn">${icon('event')}<span class="current">Event graph</span>${icon('down', 'sm')}</button></div><span class="spacer"></span><button class="btn">${icon('frame')}<span>Frame</span></button></header></div>`);
  demo('#demo-area', 'Behavior header', g);
}

// Menu (static) + live triggers
{
  const menu = h(`<div class="popup menu static"></div>`);
  menu.innerHTML = [
    `<div class="menu-item">${icon('undo')}<span class="label">Undo Move Player</span><span class="kbd">${Kit.kbd('Mod+Z')}</span></div>`,
    `<div class="menu-item" data-highlighted>${icon('redo')}<span class="label">Redo</span><span class="kbd">${Kit.kbd('Shift+Mod+Z')}</span></div>`,
    `<div class="menu-item sub">${icon('history')}<span class="label">Undo History</span><span class="icon chev sm">${String.fromCodePoint(ICONS.right)}</span></div>`,
    '<div class="menu-sep"></div><div class="menu-group">Drawers</div>',
    `<div class="menu-item" data-checked><span class="check-slot">${icon('check')}</span><span class="label">Assets</span><span class="kbd">${Kit.kbd('Mod+Shift+A')}</span></div>`,
    `<div class="menu-item"><span class="check-slot">${icon('check')}</span><span class="label">Output</span></div>`,
    '<div class="menu-sep"></div>',
    `<div class="menu-item" data-disabled>${icon('trash')}<span class="label">Delete</span><span class="kbd">X</span></div>`,
  ].join('');
  demo('#demo-menu', 'States', menu);
  const bar = h(`<div class="surface" style="padding:4px"><nav class="menus row" style="gap:0">${['File', 'Edit', 'View'].map((m) => `<button class="btn" style="color:var(--fg)" data-menu="${m.toLowerCase()}">${m}</button>`).join('')}</nav></div>`);
  const items = { file: () => [{ label: 'Search', icon: 'search', kbd: 'Mod+K' }, { label: 'Save', icon: 'save', kbd: 'Mod+S' }, '-', { label: 'Open Recent', items: [{ label: 'Sandbox', icon: 'scene' }, { label: 'Movement', icon: 'graph' }] }], edit: () => [{ label: 'Undo', icon: 'undo', kbd: 'Mod+Z' }, { label: 'Redo', icon: 'redo', kbd: 'Shift+Mod+Z', disabled: true }], view: () => [{ label: 'Reduce Motion', checked: false }, { label: 'Frame Selected', kbd: 'F' }] };
  Kit.menubar(bar.firstChild, items);
  demo('#demo-menu', 'Menubar (live)', bar);
  const ctx = h('<div class="surface w240" style="height:90px;display:flex;align-items:center;justify-content:center;color:var(--fg-3)">Right-click</div>');
  ctx.addEventListener('contextmenu', (e) => Kit.contextMenu(e, [{ group: 'Player' }, { label: 'Move', kbd: 'G', icon: 'move' }, { label: 'Rotate', kbd: 'R', icon: 'rotate' }, '-', { label: 'Delete', kbd: 'X', icon: 'trash' }]));
  demo('#demo-menu', 'Context menu (live)', ctx);
}

// Tooltip
demo('#demo-tooltip', 'Static', h(`<div class="tooltip static"><span>Frame selected</span><span class="kbd">F</span></div>`));
demo('#demo-tooltip', 'Live group', h(`<div class="row" style="gap:2px">${['play', 'stop', 'step'].map((n, i) => `<button class="btn icon-only subtle" data-tip="${['Play', 'Stop', 'Step'][i]}" data-kbd="${['Space', 'Shift+Space', 'Alt+Space'][i]}">${icon(n)}</button>`).join('')}</div>`));

// Command (live)
{
  const btn = h(`<button class="btn subtle">${icon('search')}<span>Open command</span><span class="kbd">${Kit.kbd('Mod+K')}</span></button>`);
  const items = () => [
    { id: 'a', group: 'Recent', label: 'Sandbox', icon: 'scene', meta: 'scenes' },
    { id: 'b', group: 'Recent', label: 'Movement', icon: 'graph', meta: 'behaviors' },
    { id: 'c', group: 'Commands', label: 'Save', icon: 'save', kbd: 'Mod+S' },
    { id: 'd', group: 'Commands', label: 'Frame Selected', icon: 'frame', kbd: 'F', keywords: ['focus'] },
    { id: 'e', group: 'Commands', label: 'Add Object…', icon: 'add', page: 'Add Object', items: () => ['Player', 'Swarmer', 'Crystal', 'Cloud'].map((p) => ({ id: p, group: 'Prototypes', label: p, icon: 'prototype', action: 'Add' })) },
  ];
  btn.addEventListener('click', () => Kit.command({ items }));
  demo('#demo-command', 'Live', btn);
}

// Dialog (static)
demo('#demo-dialog', 'Close dirty document', h(`<div class="command static" style="width:360px"><div style="padding:16px 16px 12px;font-size:13px;font-weight:600">Save changes to Movement?</div><div class="row" style="justify-content:flex-end;padding:0 12px 12px;gap:6px"><button class="btn subtle">Cancel</button><button class="btn outline danger">Discard</button><button class="btn primary">Save</button></div></div>`));

// Node
{
  const wrap = h('<div class="surface node-demo" style="background:var(--canvas);width:340px;height:170px"></div>');
  wrap.innerHTML = `<div class="node" data-kind="Branch" data-selected style="left:20px;top:14px;position:absolute">
    <div class="node-head">${icon('branch')}<span class="title">Branch</span></div>
    <div class="node-body">
      <div class="pin-row pair"><div class="pin-row in"><span class="socket exec" style="background:var(--t-exec)"></span></div><div class="pin-row out"><span class="pin-label">True</span><span class="socket exec" style="background:var(--t-exec)"></span></div></div>
      <div class="pin-row out"><span class="pin-label">False</span><span class="socket exec" style="background:var(--t-exec)"></span></div>
      <div class="pin-row in"><span class="socket" style="background:var(--t-bool)"></span><span class="pin-label">Condition</span><span class="binding">${icon('field')}<span>ActionBinding.pressed</span></span></div>
      <div class="pin-row in"><span class="socket" data-hollow style="color:var(--t-float)"></span><span class="pin-label">Scale</span><span class="slot"></span></div>
    </div></div>`;
  const slot = wrap.querySelector('.slot');
  const f = box(); f.style.flex = '1'; f.style.height = '18px'; slot.replaceWith(f); Kit.numberField(f, { value: 2, step: 0.1 });
  demo('#demo-node', 'Selected node, exec and data sockets', wrap);
  const types = h(`<div class="surface" style="display:flex;flex-direction:column;gap:6px">${Object.entries({ Exec: '--t-exec', Float: '--t-float', Int: '--t-int', Bool: '--t-bool', Vector: '--t-vec', Color: '--t-color', String: '--t-string', Entity: '--t-entity' }).map(([n, v]) => `<span class="row" style="gap:8px"><span style="width:10px;height:10px;border-radius:${n === 'Exec' ? '2px;transform:rotate(45deg)' : '5px'};background:var(${v});display:inline-block"></span><span class="muted">${n}</span></span>`).join('')}</div>`);
  demo('#demo-node', 'Socket types', types);
}

// Status
demo('#demo-status', 'Viewport', h(`<footer class="statusbar" style="width:720px"><span class="hints">${[['mouse', 'Select'], ['Shift', 'Extend'], ['pan', 'Pan (middle)'], ['G', 'Move'], ['R', 'Rotate'], ['Shift+A', 'Add']].map(([k, l]) => `<span class="hint">${ICONS[k] ? icon(k) : k.split(' ').map((x) => `<span class="kbd-key">${Kit.kbd(x)}</span>`).join('')}<span>${l}</span></span>`).join('')}</span><span class="spacer"></span><span class="report">${icon('check')}<span>Saved Sandbox</span></span></footer>`));
demo('#demo-status', 'Modal move', h(`<footer class="statusbar" style="width:720px"><span class="hints">${[['mouse', 'Confirm'], ['Esc', 'Cancel'], ['X', 'Along X'], ['Y', 'Along Y'], ['Ctrl', 'Snap'], ['move', 'Δ 64, -32']].map(([k, l]) => `<span class="hint">${ICONS[k] ? icon(k) : `<span class="kbd-key">${Kit.kbd(k)}</span>`}<span>${l}</span></span>`).join('')}</span></footer>`));
})();
