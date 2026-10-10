/* Studio core: document model, history, overrides, areas, menus, commands.
   The native port keeps this model in C; the view only reads state and emits actions. */
'use strict';
(() => {
const { $, $$, h, clamp } = Kit;
const F = window.FIXTURE;
const short = (k) => k.split('.').pop();
const human = (s) => s.replace(/^Game(?=[A-Z])/, '').replace(/^Material(?=[A-Z])/, 'Material ').replace(/_/g, ' ').replace(/([a-z])([A-Z])/g, '$1 $2').replace(/^./, (c) => c.toUpperCase());
const clone = (v) => JSON.parse(JSON.stringify(v));

/* ---------- Model ---------- */
const prototypes = {};
for (const [name, comps] of Object.entries(F.prototypes)) {
  prototypes[name] = {};
  for (const [k, v] of Object.entries(comps)) prototypes[name][short(k)] = clone(v);
}
let nextId = 1;
const entities = F.scene.entities.map((e) => ({
  id: nextId++, name: e.name, prototype: e.prototype, overrides: clone(e.components || {}), behaviors: [...(e.behaviors || [])], hidden: false, locked: false,
}));
const PROTO_ICON = { Player: 'triangle', Camera: 'camera', HRC: 'light', FireMouse: 'mouse', FireStick: 'gameplay', Cloud: 'cloud', Liquid: 'water', Swarmer: 'triangle', Cave: 'hexagon', Crystal: 'diamond', HardRock: 'cube', FuelCloud: 'cloud', FreeMotionSystem: 'system', DriftingMarker: 'circle' };
const protoIcon = (p) => PROTO_ICON[p] || 'cube';

function getPath(obj, path) { return path.split('.').reduce((o, k) => (o == null ? undefined : o[k]), obj); }
function setPath(obj, path, v) {
  const keys = path.split('.');
  let o = obj;
  for (const k of keys.slice(0, -1)) o = o[k] ??= {};
  o[keys[keys.length - 1]] = v;
}
function deletePath(obj, path) {
  const keys = path.split('.');
  const stack = [obj];
  for (const k of keys.slice(0, -1)) { const n = stack[stack.length - 1]?.[k]; if (!n) return; stack.push(n); }
  delete stack[stack.length - 1][keys[keys.length - 1]];
  for (let i = keys.length - 2; i >= 0; i--) if (Object.keys(stack[i + 1]).length === 0) delete stack[i][keys[i]];
}

/* The subject of editing: a scene instance, or a prototype opened as its own document. */
function subjectComponents(s) {
  if (s.kind === 'prototype') return Object.keys(prototypes[s.name] || {});
  const proto = prototypes[s.entity.prototype] || {};
  return [...new Set([...Object.keys(proto), ...Object.keys(s.entity.overrides)])];
}
function read(s, comp, path) {
  if (s.kind === 'prototype') return getPath(prototypes[s.name][comp], path);
  if (State.runtime) { const r = runtimeRead(s.entity, comp, path); if (r !== undefined) return r; }
  const o = getPath(s.entity.overrides[comp], path);
  if (o !== undefined && typeof o !== 'object') return o;
  return getPath(prototypes[s.entity.prototype]?.[comp], path);
}
function overridden(s, comp, path) { return s.kind === 'scene' && getPath(s.entity.overrides[comp], path) !== undefined; }
function componentOverrides(s, comp) {
  if (s.kind !== 'scene') return 0;
  const count = (v) => (v && typeof v === 'object' ? Object.values(v).reduce((a, x) => a + count(x), 0) : 1);
  return s.entity.overrides[comp] ? count(s.entity.overrides[comp]) : 0;
}

/* ---------- Documents and history ---------- */
const docs = [
  { id: 'scene:Sandbox', kind: 'scene', name: 'Sandbox', icon: 'scene', changes: 0, saved: 0 },
  { id: 'behavior:Movement', kind: 'behavior', name: 'Movement', icon: 'graph', changes: 0, saved: 0 },
];
const doc = (id) => docs.find((d) => d.id === id);
const dirty = (d) => d.changes !== d.saved;
const history = { undo: [], redo: [] };
function record(entry) {
  entry.redo();
  history.undo.push(entry);
  history.redo.length = 0;
  touch(entry.doc, 1);
  App.refresh(entry.what || 'all');
}
function touch(docId, d) { const x = doc(docId); if (x) x.changes += d; }
function undo() {
  const e = history.undo.pop();
  if (!e) return;
  e.undo(); history.redo.push(e); touch(e.doc, -1);
  App.report(`Undo ${e.label}`); App.refresh(e.what || 'all');
}
function redo() {
  const e = history.redo.pop();
  if (!e) return;
  e.redo(); history.undo.push(e); touch(e.doc, 1);
  App.report(`Redo ${e.label}`); App.refresh(e.what || 'all');
}
function save(all) {
  const targets = all ? docs.filter(dirty) : [doc(State.doc)].filter(dirty);
  if (!targets.length) { App.report('No changes to save', 'info'); return; }
  targets.forEach((d) => (d.saved = d.changes));
  App.report(`Saved ${targets.map((d) => d.name).join(', ')}`);
  App.refresh('docs');
}
function openDoc(kind, name) {
  const id = `${kind}:${name}`;
  if (!doc(id)) docs.push({ id, kind, name, icon: kind === 'prototype' ? protoIcon(name) : kind === 'behavior' ? 'graph' : 'scene', changes: 0, saved: 0 });
  State.doc = id;
  if (kind === 'behavior') { State.graph.behavior = name; State.graph.index = 0; State.graph.selected = []; }
  if (kind === 'prototype') State.protoSelected = true;
  App.refresh('all');
}
function closeDoc(id) {
  const d = doc(id);
  if (!d || d.kind === 'scene') return;
  if (dirty(d)) {
    App.confirm(`Save changes to ${d.name}?`, [
      { label: 'Cancel' },
      { label: 'Discard', variant: 'danger', run: () => { d.saved = d.changes; closeDoc(id); } },
      { label: 'Save', variant: 'primary', run: () => { d.saved = d.changes; closeDoc(id); } },
    ]);
    return;
  }
  docs.splice(docs.indexOf(d), 1);
  if (State.doc === id) State.doc = docs[0].id;
  App.refresh('all');
}

/* ---------- Editing actions (each is one undo step) ---------- */
function subjectDoc(s) { return s.kind === 'prototype' ? `prototype:${s.name}` : 'scene:Sandbox'; }
function setValue(s, comp, path, value, before, label) {
  const target = s.kind === 'prototype' ? prototypes[s.name] : s.entity.overrides;
  const wasOverride = s.kind === 'scene' ? getPath(s.entity.overrides[comp], path) : undefined;
  const prev = before !== undefined ? before : read(s, comp, path);
  record({
    label: label || `${human(comp)} ${human(path.split('.').pop())}`, doc: subjectDoc(s), what: 'values',
    redo: () => { target[comp] ??= {}; setPath(target[comp], path, value); },
    undo: () => {
      if (s.kind === 'scene' && wasOverride === undefined) deletePath(s.entity.overrides, `${comp}.${path}`);
      else { target[comp] ??= {}; setPath(target[comp], path, s.kind === 'scene' ? wasOverride : prev); }
    },
  });
}
function resetOverride(s, comp, path) {
  const prev = clone(s.entity.overrides[comp] ?? {});
  record({
    label: `Reset ${human(path ? path.split('.').pop() : comp)}`, doc: 'scene:Sandbox', what: 'values',
    redo: () => (path ? deletePath(s.entity.overrides, `${comp}.${path}`) : delete s.entity.overrides[comp]),
    undo: () => { s.entity.overrides[comp] = clone(prev); },
  });
}
function applyToPrototype(s, comp, path) {
  const v = getPath(s.entity.overrides[comp], path);
  const proto = prototypes[s.entity.prototype];
  const prev = getPath(proto[comp], path);
  record({
    label: `Apply ${human(path.split('.').pop())} to ${s.entity.prototype}`, doc: `prototype:${s.entity.prototype}`, what: 'values',
    redo: () => { setPath(proto[comp], path, v); deletePath(s.entity.overrides, `${comp}.${path}`); },
    undo: () => { setPath(proto[comp], path, prev); s.entity.overrides[comp] ??= {}; setPath(s.entity.overrides[comp], path, v); },
  });
  if (!doc(`prototype:${s.entity.prototype}`)) docs.push({ id: `prototype:${s.entity.prototype}`, kind: 'prototype', name: s.entity.prototype, icon: protoIcon(s.entity.prototype), changes: 1, saved: 0 });
  App.refresh('docs');
}
function position(e) {
  return { x: read({ kind: 'scene', entity: e }, 'GameTransform', 'x') ?? 0, y: read({ kind: 'scene', entity: e }, 'GameTransform', 'y') ?? 0 };
}
function moveEntities(list, before, after, label) {
  record({
    label, doc: 'scene:Sandbox', what: 'values',
    redo: () => list.forEach((e, i) => { e.overrides.GameTransform ??= {}; Object.assign(e.overrides.GameTransform, after[i]); }),
    undo: () => list.forEach((e, i) => { e.overrides.GameTransform ??= {}; Object.assign(e.overrides.GameTransform, before[i]); }),
  });
}
function renameEntity(e, name) {
  const prev = e.name;
  if (!name || name === prev) return;
  record({ label: `Rename ${prev}`, doc: 'scene:Sandbox', what: 'all', redo: () => (e.name = name), undo: () => (e.name = prev) });
}
function deleteEntities(list) {
  if (!list.length) return;
  const at = list.map((e) => entities.indexOf(e));
  record({
    label: list.length === 1 ? `Delete ${list[0].name}` : `Delete ${list.length} objects`, doc: 'scene:Sandbox', what: 'all',
    redo: () => { list.forEach((e) => entities.splice(entities.indexOf(e), 1)); State.selection = []; },
    undo: () => { list.map((e, i) => [e, at[i]]).sort((a, b) => a[1] - b[1]).forEach(([e, i]) => entities.splice(i, 0, e)); State.selection = list.map((e) => e.id); },
  });
}
function addEntity(prototype, at, name) {
  const e = { id: nextId++, name: name || uniqueName(prototype), prototype, overrides: { GameTransform: { x: Math.round(at.x), y: Math.round(at.y) } }, behaviors: [], hidden: false, locked: false };
  record({
    label: `Add ${e.name}`, doc: 'scene:Sandbox', what: 'all',
    redo: () => { entities.push(e); State.selection = [e.id]; },
    undo: () => { entities.splice(entities.indexOf(e), 1); State.selection = []; },
  });
  return e;
}
function duplicateEntities(list) {
  const copies = list.map((e) => ({ ...clone(e), id: nextId++, name: uniqueName(e.name.replace(/\s\d+$/, '')) }));
  copies.forEach((c) => { c.overrides.GameTransform ??= {}; const p = position(list[copies.indexOf(c)]); c.overrides.GameTransform.x = p.x + 24; c.overrides.GameTransform.y = p.y - 24; });
  record({
    label: `Duplicate ${list.length === 1 ? list[0].name : `${list.length} objects`}`, doc: 'scene:Sandbox', what: 'all',
    redo: () => { entities.push(...copies); State.selection = copies.map((c) => c.id); },
    undo: () => { copies.forEach((c) => entities.splice(entities.indexOf(c), 1)); State.selection = list.map((e) => e.id); },
  });
}
function uniqueName(base) {
  let n = 1, name = base;
  while (entities.some((e) => e.name === name)) name = `${base} ${++n}`;
  return name;
}
function toggleBehavior(e, name) {
  const had = e.behaviors.includes(name);
  record({
    label: `${had ? 'Detach' : 'Attach'} ${name}`, doc: 'scene:Sandbox', what: 'all',
    redo: () => (had ? e.behaviors.splice(e.behaviors.indexOf(name), 1) : e.behaviors.push(name)),
    undo: () => (had ? e.behaviors.push(name) : e.behaviors.splice(e.behaviors.indexOf(name), 1)),
  });
}

/* ---------- State ---------- */
const State = {
  doc: 'scene:Sandbox',
  selection: [entities[0].id],
  graph: { behavior: 'Movement', index: 0, selected: [] },
  runtime: null, // null | { playing, time, frame }
  target: 'scene',
  drawer: null,
  tool: 'select',
  shading: 'solid',
  overlays: { grid: true, labels: true, helpers: true },
  propTab: 'object',
  propSearch: '',
  outlinerFilter: '',
  openPanels: { GameTransform: true, GameOrientation: true, GameAppearance: true, GameMovement: true, GameBody: true, GameCollider: true, GameMaterial: false, GameHealth: true, GameWeapon: true, GameCamera: true, GameFluidVolume: true, scenePhysics: true, behaviors: true, sceneSettings: true },
};
const selected = () => entities.filter((e) => State.selection.includes(e.id));
const activeEntity = () => entities.find((e) => e.id === State.selection[State.selection.length - 1]);
function subject() {
  const d = doc(State.doc);
  if (d.kind === 'prototype') return { kind: 'prototype', name: d.name };
  const e = activeEntity();
  return e ? { kind: 'scene', entity: e } : null;
}
function select(ids, mode) {
  if (mode === 'toggle') State.selection = State.selection.includes(ids[0]) ? State.selection.filter((x) => x !== ids[0]) : [...State.selection, ids[0]];
  else if (mode === 'extend') State.selection = [...new Set([...State.selection, ...ids])];
  else State.selection = ids;
  if (doc(State.doc).kind === 'prototype') State.doc = 'scene:Sandbox';
  App.refresh('selection');
}

/* ---------- Runtime (transport) ---------- */
let raf = 0;
function play() {
  if (State.runtime?.playing) { State.runtime.playing = false; App.refresh('runtime'); return; }
  if (!State.runtime) {
    State.runtime = { playing: true, time: 0, frame: 0 };
    entities.forEach((e) => { e.runtime = {}; e.start = position(e); });
  }
  State.runtime.playing = true;
  let last = performance.now();
  cancelAnimationFrame(raf);
  const tick = (now) => {
    if (!State.runtime?.playing) return;
    step((now - last) / 1000);
    last = now;
    raf = requestAnimationFrame(tick);
  };
  raf = requestAnimationFrame(tick);
  App.refresh('runtime');
}
function step(dt = 1 / 60) {
  const r = State.runtime;
  if (!r) return;
  r.time += dt; r.frame++;
  const player = entities.find((e) => e.prototype === 'Player');
  for (const e of entities) {
    if (!e.start) continue;
    const t = r.time;
    let p = { ...e.start };
    if (e === player) { p = { x: e.start.x + Math.sin(t * 0.9) * 140, y: e.start.y + Math.sin(t * 1.8) * 60 }; e.runtime.GameOrientation = { radians: Math.atan2(Math.cos(t * 1.8) * 108, Math.cos(t * 0.9) * 126) }; }
    else if (e.prototype === 'Swarmer' && player?.runtime.GameTransform) {
      const cur = e.runtime.GameTransform || e.start, tp = player.runtime.GameTransform;
      const dx = tp.x - cur.x, dy = tp.y - cur.y, d = Math.hypot(dx, dy) || 1, sp = Math.min(d - 40, 90 * dt);
      p = { x: cur.x + dx / d * Math.max(sp, 0), y: cur.y + dy / d * Math.max(sp, 0) };
    } else if (e.prototype === 'DriftingMarker') p = { x: e.start.x + 12 * t, y: e.start.y };
    else if (e.prototype === 'Camera' && player?.runtime.GameTransform) p = { ...player.runtime.GameTransform };
    e.runtime.GameTransform = p;
  }
  App.refresh('runtime-tick');
}
function stop() {
  cancelAnimationFrame(raf);
  State.runtime = null;
  entities.forEach((e) => { delete e.runtime; delete e.start; });
  App.refresh('runtime');
}
function stepOnce() { if (!State.runtime) { play(); State.runtime.playing = false; } else State.runtime.playing = false; step(); App.refresh('runtime'); }
/* read() consults runtime values keyed by component → path. */
const runtimeRead = (e, comp, path) => e.runtime?.[comp] && getPath(e.runtime[comp], path);

/* ---------- Areas (Blender: every area has a header and an editor type) ---------- */
const EDITORS = {};
const EDITOR_TYPES = [
  { id: 'viewport', label: 'Scene', icon: 'viewport', kbd: 'Shift+F5' },
  { id: 'graph', label: 'Behavior', icon: 'graph', kbd: 'Shift+F3' },
  { id: 'outliner', label: 'Outliner', icon: 'outliner', kbd: 'Shift+F9' },
  { id: 'properties', label: 'Properties', icon: 'properties', kbd: 'Shift+F7' },
  { id: 'assets', label: 'Assets', icon: 'assets', kbd: 'Shift+F1' },
  { id: 'output', label: 'Output', icon: 'output', kbd: 'Shift+F4' },
];
const areas = [];
function mountArea(el, type) {
  const area = { el, type, instance: null };
  el.classList.add('area');
  el.innerHTML = '<header class="area-head"></header><div class="area-body"></div>';
  el.addEventListener('pointerenter', () => { App.hoverArea = area; App.setHints(area.instance?.hints?.() || []); });
  setType(area, type);
  areas.push(area);
  return area;
}
function setType(area, type) {
  area.instance?.destroy?.();
  area.type = type;
  area.el.dataset.editor = type;
  const head = $('.area-head', area.el), body = $('.area-body', area.el);
  head.innerHTML = '';
  body.innerHTML = '';
  const t = EDITOR_TYPES.find((x) => x.id === type);
  const typeBtn = h(`<button class="btn editor-type" data-tip="${t.label}" data-kbd="${t.kbd}">${icon(t.icon)}${icon('down', 'caret')}</button>`);
  typeBtn.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    Kit.openMenu([{ group: 'Editor' }, ...EDITOR_TYPES.map((x) => ({ label: x.label, icon: x.icon, kbd: x.kbd, checked: x.id === area.type, run: () => setType(area, x.id) }))], typeBtn, { anchor: typeBtn });
  });
  head.appendChild(typeBtn);
  area.instance = EDITORS[type](area, head, body);
  area.instance.update?.('all');
}
function toggleMaximize(area) {
  if (!area) return;
  const on = !area.el.hasAttribute('data-maximized');
  areas.forEach((a) => a.el.removeAttribute('data-maximized'));
  if (on) { area.el.setAttribute('data-maximized', ''); $('.workspace').appendChild(area.el); }
  else layout.restore(area);
  requestAnimationFrame(() => App.refresh('layout'));
}

/* ---------- Layout ---------- */
const layout = {
  sideWidth: 372, mainSplit: 0.6, sideSplit: 0.36, drawerHeight: 250,
  build() {
    const ws = $('.workspace');
    ws.innerHTML = `
      <div class="column main">
        <div class="area" id="area-top"></div><div class="split-h" id="split-main"></div>
        <div class="area" id="area-bottom"></div>
        <div class="drawer-slot" id="drawer" style="height:0"><div class="split-h"></div></div>
      </div>
      <div class="split-v" id="split-side"></div>
      <div class="column side">
        <div class="area" id="area-outliner"></div><div class="split-h" id="split-side-h"></div>
        <div class="area" id="area-props"></div>
      </div>`;
    this.slots = { top: $('#area-top'), bottom: $('#area-bottom'), outliner: $('#area-outliner'), props: $('#area-props') };
    mountArea(this.slots.top, 'viewport');
    mountArea(this.slots.bottom, 'graph');
    mountArea(this.slots.outliner, 'outliner');
    mountArea(this.slots.props, 'properties');
    Kit.splitter($('#split-side'), { axis: 'x', invert: true, get: () => this.sideWidth, set: (v) => { this.sideWidth = clamp(v, 260, 640); this.apply(); }, reset: () => { this.sideWidth = 372; this.apply(); } });
    Kit.splitter($('#split-main'), { axis: 'y', get: () => this.mainSplit * this.mainHeight(), set: (v) => { this.mainSplit = clamp(v / this.mainHeight(), 0.2, 0.85); this.apply(); }, reset: () => { this.mainSplit = 0.6; this.apply(); } });
    Kit.splitter($('#split-side-h'), { axis: 'y', get: () => this.sideSplit * $('.column.side').clientHeight, set: (v) => { this.sideSplit = clamp(v / $('.column.side').clientHeight, 0.15, 0.8); this.apply(); }, reset: () => { this.sideSplit = 0.36; this.apply(); } });
    addEventListener('resize', () => this.apply());
    this.apply();
  },
  mainHeight() { return $('.column.main').clientHeight - (State.drawer ? this.drawerHeight : 0) - 4; },
  apply() {
    $('.column.side').style.width = `${this.sideWidth}px`;
    const mh = this.mainHeight();
    this.slots.top.style.height = `${Math.round(mh * this.mainSplit)}px`;
    this.slots.bottom.style.flex = '1';
    const sh = $('.column.side').clientHeight - 4;
    this.slots.outliner.style.height = `${Math.round(sh * this.sideSplit)}px`;
    this.slots.props.style.flex = '1';
    App.refresh('layout');
  },
  restore(area) {
    const slot = Object.values(this.slots).find((s) => s === area.el);
    const map = { 'area-top': ['.column.main', 0], 'area-bottom': ['.column.main', 2], 'area-outliner': ['.column.side', 0], 'area-props': ['.column.side', 2] };
    const [col, i] = map[area.el.id] || [];
    if (col) $(col).insertBefore(area.el, $(col).children[i] || null);
    else if (slot) slot.appendChild(area.el);
  },
  drawer(type) {
    const slot = $('#drawer');
    const closing = !type || State.drawer === type;
    if (closing) {
      State.drawer = null;
      slot.setAttribute('data-closing', '');
      slot.style.height = '0px';
      setTimeout(() => { if (!State.drawer) { const a = areas.find((x) => x.el.parentElement === slot); if (a) { a.instance?.destroy?.(); areas.splice(areas.indexOf(a), 1); a.el.remove(); } } }, 120);
    } else {
      slot.removeAttribute('data-closing');
      let a = areas.find((x) => x.el.parentElement === slot);
      if (!a) { const el = h('<div class="area"></div>'); slot.appendChild(el); a = mountArea(el, type); } else if (a.type !== type) setType(a, type);
      State.drawer = type;
      slot.style.height = `${this.drawerHeight}px`;
    }
    this.apply();
    setTimeout(() => this.apply(), 220);
    App.refresh('drawer');
  },
};

/* ---------- Top bar ---------- */
function buildTopbar() {
  const bar = $('.topbar');
  bar.innerHTML = `
    <span class="brand"><span class="mark"></span></span>
    <nav class="menus" role="menubar">${['File', 'Edit', 'View', 'Object', 'Play'].map((m) => `<button class="btn" data-menu="${m.toLowerCase()}">${m}</button>`).join('')}</nav>
    <span class="sep"></span>
    <nav class="doc-tabs"></nav>
    <span class="spacer"></span>
    <div class="transport">
      <div class="target"></div>
      <div class="group">
        <button class="btn play" data-tip="Play" data-kbd="Space">${icon('play')}</button>
        <button class="btn stop" data-tip="Stop" data-kbd="Shift+Space">${icon('stop')}</button>
        <button class="btn step" data-tip="Step" data-kbd="Alt+Space">${icon('step')}</button>
      </div>
      <span class="clock">0:00.00</span>
    </div>
    <span class="spacer"></span>
    <button class="btn search-trigger" data-action="palette">${icon('search')}<span>Search</span><span class="kbd">${Kit.kbd('Mod+K')}</span></button>`;
  Kit.menubar($('.menus', bar), MENUS);
  $('.play', bar).addEventListener('click', play);
  $('.stop', bar).addEventListener('click', stop);
  $('.step', bar).addEventListener('click', stepOnce);
  $('.search-trigger', bar).addEventListener('click', () => palette());
  App.targetSelect = Kit.select($('.target', bar), {
    options: [{ value: 'scene', label: 'Scene · Sandbox', icon: 'scene' }, ...docs.filter((d) => d.kind === 'prototype').map((d) => ({ value: d.id, label: `Prototype · ${d.name}`, icon: d.icon }))],
    value: () => State.target, onChange: (v) => { State.target = v; }, readOnly: () => !!State.runtime,
  });
}
function renderDocTabs() {
  const nav = $('.doc-tabs');
  nav.innerHTML = docs.map((d) => `<button class="doc-tab" data-id="${d.id}" ${d.id === State.doc ? 'data-active' : ''} ${dirty(d) ? 'data-dirty' : ''} data-tip="${d.kind === 'scene' ? 'scenes' : d.kind === 'behavior' ? 'behaviors' : 'prototypes'}/${d.name}.json">${icon(d.icon)}<span>${d.name}</span><span class="dirty"></span>${d.kind === 'scene' ? '' : `<span class="close" data-close>${icon('close')}</span>`}</button>`).join('');
  $$('.doc-tab', nav).forEach((t) => {
    t.addEventListener('click', (e) => {
      if (e.target.closest('[data-close]')) { closeDoc(t.dataset.id); return; }
      const d = doc(t.dataset.id);
      openDoc(d.kind, d.name);
    });
    t.addEventListener('auxclick', (e) => { if (e.button === 1) closeDoc(t.dataset.id); });
    t.addEventListener('contextmenu', (e) => Kit.contextMenu(e, [
      { label: 'Save', kbd: 'Mod+S', disabled: !dirty(doc(t.dataset.id)), run: () => { State.doc = t.dataset.id; save(); } },
      { label: 'Close', disabled: doc(t.dataset.id).kind === 'scene', run: () => closeDoc(t.dataset.id) },
      '-', { label: 'Copy Path', icon: 'copy', run: () => App.report('Copied path') },
    ]));
  });
  const tr = $('.transport');
  tr.toggleAttribute('data-running', !!State.runtime);
  const pb = $('.play');
  pb.innerHTML = icon(State.runtime?.playing ? 'pause' : 'play');
  pb.dataset.tip = State.runtime?.playing ? 'Pause' : 'Play';
  pb.toggleAttribute('data-state', false);
  if (State.runtime?.playing) pb.dataset.state = 'on'; else delete pb.dataset.state;
  $('.stop').toggleAttribute('data-disabled', !State.runtime);
  const t = State.runtime?.time || 0;
  $('.clock').textContent = `${Math.floor(t / 60)}:${(t % 60).toFixed(2).padStart(5, '0')}`;
}

/* ---------- Menus ---------- */
const MENUS = {
  file: () => [
    { label: 'Search', icon: 'search', kbd: 'Mod+K', run: () => palette() },
    { label: 'Open', icon: 'folderOpen', kbd: 'Mod+P', run: () => palette('assets') },
    { label: 'Open Recent', items: [{ label: 'Sandbox', icon: 'scene', run: () => openDoc('scene', 'Sandbox') }, { label: 'Movement', icon: 'graph', run: () => openDoc('behavior', 'Movement') }, { label: 'Player', icon: 'triangle', run: () => openDoc('prototype', 'Player') }] },
    '-',
    { label: 'Save', icon: 'save', kbd: 'Mod+S', disabled: !dirty(doc(State.doc)), run: () => save() },
    { label: 'Save All', kbd: 'Alt+Mod+S', disabled: !docs.some(dirty), run: () => save(true) },
    { label: 'Revert', disabled: !dirty(doc(State.doc)), run: () => { while (dirty(doc(State.doc)) && history.undo.length && history.undo[history.undo.length - 1].doc === State.doc) undo(); } },
    '-',
    { label: 'Close Document', kbd: 'Mod+W', disabled: doc(State.doc).kind === 'scene', run: () => closeDoc(State.doc) },
  ],
  edit: () => {
    const u = history.undo[history.undo.length - 1], r = history.redo[history.redo.length - 1];
    return [
      { label: u ? `Undo ${u.label}` : 'Undo', icon: 'undo', kbd: 'Mod+Z', disabled: !u, run: undo },
      { label: r ? `Redo ${r.label}` : 'Redo', icon: 'redo', kbd: 'Shift+Mod+Z', disabled: !r, run: redo },
      { label: 'Undo History', icon: 'history', disabled: !history.undo.length, items: history.undo.slice(-12).reverse().map((e, i) => ({ label: e.label, checked: i === 0, run: () => { for (let k = 0; k < i; k++) undo(); } })) },
      '-',
      { label: 'Rename', kbd: 'F2', disabled: !activeEntity(), run: () => App.rename?.() },
      { label: 'Duplicate', kbd: 'Shift+D', disabled: !selected().length, run: () => duplicateEntities(selected()) },
      { label: 'Delete', icon: 'trash', kbd: 'X', disabled: !selected().length, run: () => deleteEntities(selected()) },
      '-',
      { label: 'Select All', kbd: 'A', run: () => select(entities.map((e) => e.id)) },
      { label: 'Deselect', kbd: 'Alt+A', run: () => select([]) },
    ];
  },
  view: () => [
    { label: 'Frame Selected', icon: 'focus', kbd: 'F', run: () => App.frame?.('selected') },
    { label: 'Frame All', kbd: 'Home', run: () => App.frame?.('all') },
    { label: 'Maximize Area', icon: 'maximize', kbd: 'Ctrl+Space', run: () => toggleMaximize(App.hoverArea || areas[0]) },
    '-',
    { group: 'Drawers' },
    { label: 'Assets', checked: State.drawer === 'assets', kbd: 'Mod+Shift+A', run: () => layout.drawer('assets') },
    { label: 'Output', checked: State.drawer === 'output', kbd: 'Mod+Shift+U', run: () => layout.drawer('output') },
    '-',
    { label: 'Density', items: [
      { label: 'Compact', checked: document.documentElement.dataset.density !== 'comfortable', run: () => setDensity('compact') },
      { label: 'Comfortable', checked: document.documentElement.dataset.density === 'comfortable', run: () => setDensity('comfortable') },
    ] },
    { label: 'Reduce Motion', checked: document.documentElement.dataset.motion === 'reduced', run: () => setMotion(document.documentElement.dataset.motion !== 'reduced') },
    '-',
    { label: 'Reset Layout', run: () => { layout.sideWidth = 372; layout.mainSplit = 0.6; layout.sideSplit = 0.36; areas.forEach((a) => a.el.removeAttribute('data-maximized')); layout.apply(); } },
  ],
  object: () => [
    { label: 'Add', icon: 'add', kbd: 'Shift+A', items: Object.keys(prototypes).filter((p) => prototypes[p].GameTransform).map((p) => ({ label: p, icon: protoIcon(p), run: () => addEntity(p, App.viewCenter?.() || { x: 0, y: 0 }) })) },
    { label: 'Duplicate', kbd: 'Shift+D', disabled: !selected().length, run: () => duplicateEntities(selected()) },
    { label: 'Delete', icon: 'trash', kbd: 'X', disabled: !selected().length, run: () => deleteEntities(selected()) },
    '-',
    { label: 'Open Prototype', icon: 'prototype', disabled: !activeEntity(), run: () => openDoc('prototype', activeEntity().prototype) },
    { label: 'Attach Behavior', icon: 'graph', disabled: !activeEntity(), run: () => palette('behaviors') },
    '-',
    { label: 'Hide', kbd: 'H', disabled: !selected().length, run: () => { selected().forEach((e) => (e.hidden = true)); App.refresh('all'); } },
    { label: 'Show All', kbd: 'Alt+H', run: () => { entities.forEach((e) => (e.hidden = false)); App.refresh('all'); } },
  ],
  play: () => [
    { label: State.runtime?.playing ? 'Pause' : 'Play', icon: State.runtime?.playing ? 'pause' : 'play', kbd: 'Space', run: play },
    { label: 'Stop', icon: 'stop', kbd: 'Shift+Space', disabled: !State.runtime, run: stop },
    { label: 'Step', icon: 'step', kbd: 'Alt+Space', run: stepOnce },
  ],
};
function setDensity(d) { document.documentElement.dataset.density = d; layout.apply(); App.refresh('all'); }
function setMotion(on) { if (on) document.documentElement.dataset.motion = 'reduced'; else delete document.documentElement.dataset.motion; App.refresh('study'); }

/* ---------- Command palette content (cmdk) ---------- */
function commandItems() {
  const cmds = [
    { label: 'Save', kbd: 'Mod+S', icon: 'save', run: () => save() },
    { label: 'Save All', kbd: 'Alt+Mod+S', icon: 'save', run: () => save(true) },
    { label: 'Undo', kbd: 'Mod+Z', icon: 'undo', run: undo },
    { label: 'Redo', kbd: 'Shift+Mod+Z', icon: 'redo', run: redo },
    { label: State.runtime?.playing ? 'Pause' : 'Play', kbd: 'Space', icon: 'play', keywords: ['run', 'start', 'preview'], run: play },
    { label: 'Stop', kbd: 'Shift+Space', icon: 'stop', run: stop },
    { label: 'Frame Selected', kbd: 'F', icon: 'focus', keywords: ['focus', 'zoom'], run: () => App.frame?.('selected') },
    { label: 'Frame All', kbd: 'Home', icon: 'frame', run: () => App.frame?.('all') },
    { label: 'Toggle Assets', kbd: 'Mod+Shift+A', icon: 'assets', keywords: ['browser', 'content'], run: () => layout.drawer('assets') },
    { label: 'Toggle Output', kbd: 'Mod+Shift+U', icon: 'output', keywords: ['log', 'console', 'diagnostics'], run: () => layout.drawer('output') },
    { label: 'Add Object…', icon: 'add', page: 'Add Object', items: addObjectItems },
    { label: 'Attach Behavior…', icon: 'graph', page: 'Attach Behavior', items: behaviorItems },
    { label: 'Duplicate', kbd: 'Shift+D', icon: 'copy', run: () => duplicateEntities(selected()) },
    { label: 'Delete', kbd: 'X', icon: 'trash', run: () => deleteEntities(selected()) },
    { label: 'Reset Layout', icon: 'layout', run: () => MENUS.view().find((x) => x.label === 'Reset Layout').run() },
    { label: 'Comfortable Density', icon: 'layout', keywords: ['spacing', 'size'], run: () => setDensity('comfortable') },
    { label: 'Compact Density', icon: 'layout', keywords: ['spacing', 'size'], run: () => setDensity('compact') },
    { label: 'Reduce Motion', icon: 'sparkle', keywords: ['animation'], run: () => setMotion(document.documentElement.dataset.motion !== 'reduced') },
  ].map((c, i) => ({ id: `cmd${i}`, group: 'Commands', ...c }));
  return cmds;
}
function objectItems() { return entities.map((e) => ({ id: `obj${e.id}`, group: 'Objects', label: e.name, icon: protoIcon(e.prototype), meta: e.prototype, keywords: [e.prototype], action: 'Select', run: () => { select([e.id]); App.frame?.('selected'); } })); }
function assetItems() {
  return F.assets.map((p, i) => {
    const [folder, file] = [p.split('/')[0], p.split('/').pop().replace(/\.json$/, '')];
    const kind = { scenes: 'scene', behaviors: 'behavior', prototypes: 'prototype' }[folder];
    const iconName = { scenes: 'scene', behaviors: 'graph', prototypes: protoIcon(file), events: 'event', libraries: 'library', materials: 'material', fields: 'grid', 'shader-libraries': 'library', systems: 'system' }[folder] || 'json';
    return { id: `asset${i}`, group: 'Assets', label: file, icon: iconName, meta: folder, keywords: [folder], run: () => (kind ? openDoc(kind, file) : App.report(`${file} opens in its asset editor`, 'info')) };
  });
}
function addObjectItems() { return Object.keys(prototypes).filter((p) => prototypes[p].GameTransform).map((p) => ({ id: `add${p}`, group: 'Prototypes', label: p, icon: protoIcon(p), action: 'Add', run: () => addEntity(p, App.viewCenter?.() || { x: 0, y: 0 }) })); }
function behaviorItems() {
  const e = activeEntity();
  return Object.keys(F.behaviors).map((b) => ({ id: `beh${b}`, group: 'Behaviors', label: b, icon: 'graph', meta: e?.behaviors.includes(b) ? 'Attached' : '', action: e?.behaviors.includes(b) ? 'Detach' : 'Attach', run: () => e && toggleBehavior(e, b) }));
}
function palette(mode) {
  if (Kit.Layers.stack.some((l) => l.el.classList.contains('command'))) return;
  const recent = [
    { id: 'r1', group: 'Recent', label: 'Sandbox', icon: 'scene', meta: 'scenes', run: () => openDoc('scene', 'Sandbox') },
    { id: 'r2', group: 'Recent', label: 'Movement', icon: 'graph', meta: 'behaviors', run: () => openDoc('behavior', 'Movement') },
    { id: 'r3', group: 'Recent', label: 'Player', icon: 'triangle', meta: 'prototypes', run: () => openDoc('prototype', 'Player') },
  ];
  const page = mode === 'assets' ? { label: 'Open', items: assetItems } : mode === 'behaviors' ? { label: 'Attach Behavior', items: behaviorItems } : mode === 'add' ? { label: 'Add Object', items: addObjectItems } : mode?.label ? mode : null;
  Kit.command({
    placeholder: 'Search objects, assets and commands',
    page,
    items: () => [...recent, ...commandItems(), ...objectItems(), ...assetItems()],
  });
}

/* ---------- Confirm dialog (Base UI Dialog) ---------- */
function confirm(title, actions) {
  const back = h('<div class="command-backdrop"></div>');
  const el = h(`<div class="command" role="alertdialog" style="width:360px;margin-left:-180px;top:30%"><div style="padding:16px 16px 12px;font-size:13px;font-weight:600">${title}</div><div class="row" style="justify-content:flex-end;padding:0 12px 12px;gap:6px">${actions.map((a, i) => `<button class="btn ${a.variant === 'primary' ? 'primary' : a.variant === 'danger' ? 'outline danger' : 'subtle'}" data-i="${i}">${a.label}</button>`).join('')}</div></div>`);
  Kit.mount(back); Kit.mount(el);
  const layer = Kit.Layers.push({ el, onClose: () => Kit.unmount(back) });
  $$('button', el).forEach((b) => b.addEventListener('click', () => { Kit.Layers.close(layer, 'close-press'); actions[+b.dataset.i].run?.(); }));
  $$('button', el).pop().focus();
}

/* ---------- Status bar ---------- */
let reportTimer = 0;
function report(text, kind = 'ok') {
  const r = $('.statusbar .report');
  r.innerHTML = `${kind === 'ok' ? icon('check') : icon('info')}<span>${text}</span>`;
  r.removeAttribute('data-fade');
  clearTimeout(reportTimer);
  reportTimer = setTimeout(() => r.setAttribute('data-fade', ''), 2400);
}
function setHints(hints) {
  $('.statusbar .hints').innerHTML = hints.map(([k, label]) => `<span class="hint">${k.startsWith('icon:') ? icon(k.slice(5)) : k.split(' ').map((x) => `<span class="kbd-key">${Kit.kbd(x)}</span>`).join('')}<span>${label}</span></span>`).join('');
}
function renderStats() {
  const sel = selected();
  $('.statusbar .stats').innerHTML = `<span><b>${doc(State.doc).name}</b></span><span>${sel.length ? `<b>${sel[sel.length - 1].name}</b>` : 'No selection'}</span><span>Objects <b>${sel.length}/${entities.length}</b></span>${State.runtime ? `<span>Frame <b>${State.runtime.frame}</b></span>` : ''}`;
  $$('.statusbar .toggle-drawer').forEach((b) => { if (State.drawer === b.dataset.drawer) b.dataset.state = 'on'; else delete b.dataset.state; });
}

/* ---------- Keyboard (Blender: shortcuts act on the area under the pointer) ---------- */
function typing() { const a = document.activeElement; return a && (a.tagName === 'INPUT' || a.isContentEditable); }
addEventListener('keydown', (e) => {
  if (e.defaultPrevented) return;
  const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
  if (mod && k === 'k') { e.preventDefault(); palette(); return; }
  if (mod && k === 'p') { e.preventDefault(); palette('assets'); return; }
  if (mod && k === 's') { e.preventDefault(); save(e.altKey); return; }
  if (mod && e.shiftKey && k === 'a') { e.preventDefault(); layout.drawer('assets'); return; }
  if (mod && e.shiftKey && k === 'u') { e.preventDefault(); layout.drawer('output'); return; }
  if (typing() || Kit.Layers.open) return;
  if (mod && k === 'z') { e.preventDefault(); if (e.shiftKey) redo(); else undo(); return; }
  if (mod && k === 'y') { e.preventDefault(); redo(); return; }
  if (e.ctrlKey && e.key === ' ') { e.preventDefault(); toggleMaximize(App.hoverArea); return; }
  if (e.key === ' ' && !document.activeElement?.closest?.('.field, .segmented, .check, .menus')) { e.preventDefault(); if (e.shiftKey) stop(); else if (e.altKey) stepOnce(); else play(); return; }
  if (App.hoverArea?.instance?.key?.(e)) { e.preventDefault(); return; }
  if (e.key === 'F2') { e.preventDefault(); App.rename?.(); }
});

/* ---------- Refresh ---------- */
function refresh(what = 'all') {
  if (what === 'runtime-tick') {
    areas.forEach((a) => a.instance?.update?.('runtime-tick'));
    renderDocTabs(); renderStats();
    return;
  }
  renderDocTabs();
  renderStats();
  areas.forEach((a) => a.instance?.update?.(what));
}

/* ---------- Study strip (outside the app) ---------- */
function buildStudy() {
  const s = $('.study');
  s.innerHTML = `<span>Studio study · Neon Swarm fixture</span><span class="spacer"></span><a href="kit.html">Components ↗</a><a href="DESIGN.md">Notes ↗</a><span class="density"></span><span class="motion"></span>`;
  Kit.toggleGroup($('.density', s), { options: [{ value: 'compact', label: 'Compact' }, { value: 'comfortable', label: 'Comfortable' }], value: () => document.documentElement.dataset.density || 'compact', onChange: setDensity });
  Kit.toggleGroup($('.motion', s), { options: [{ value: 'full', label: 'Motion' }, { value: 'reduced', label: 'Reduced' }], value: () => document.documentElement.dataset.motion || 'full', onChange: (v) => setMotion(v === 'reduced') });
}

/* Review states for captures: ?density=comfortable&select=Crystal&drawer=assets&tab=physics&search=radius&play=1&palette=cave */
function applyQuery(q) {
  if (q.get('density')) setDensity(q.get('density'));
  if (q.get('select')) { const e = entities.find((x) => x.name.toLowerCase().startsWith(q.get('select').toLowerCase())); if (e) select([e.id]); }
  if (q.get('doc')) { const [kind, name] = q.get('doc').split(':'); openDoc(kind, name); }
  if (q.get('tab')) { State.propTab = q.get('tab'); refresh('all'); }
  if (q.get('search')) { State.propSearch = q.get('search'); refresh('all'); }
  if (q.get('drawer')) layout.drawer(q.get('drawer'));
  if (q.get('play')) { play(); setTimeout(() => { if (State.runtime) State.runtime.playing = false; refresh('runtime'); }, +q.get('play') * 1000 || 1500); }
  if (q.has('palette')) setTimeout(() => { palette(); const i = document.querySelector('.command input'); if (i) { i.value = q.get('palette'); i.dispatchEvent(new Event('input')); } }, 50);
  if (q.get('menu')) setTimeout(() => document.querySelector(`[data-menu="${q.get('menu')}"]`)?.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })), 50);
}

window.App = {
  F, State, entities, prototypes, docs, doc, dirty, history, EDITORS, EDITOR_TYPES, areas, layout,
  short, human, clone, getPath, protoIcon, read, runtimeRead, overridden, componentOverrides, subject, subjectComponents, selected, activeEntity, position,
  select, setValue, resetOverride, applyToPrototype, moveEntities, renameEntity, deleteEntities, addEntity, duplicateEntities, toggleBehavior,
  openDoc, closeDoc, save, undo, redo, play, stop, stepOnce, palette, confirm, report, setHints, refresh, toggleMaximize, setType, typing,
  hoverArea: null,
  init() {
    Kit.report = report;
    buildTopbar();
    layout.build();
    buildStudy();
    $$('.statusbar .toggle-drawer').forEach((b) => b.addEventListener('click', () => layout.drawer(b.dataset.drawer)));
    refresh('all');
    setHints([['icon:mouse', 'Select'], ['Mod+K', 'Search']]);
    applyQuery(new URLSearchParams(location.search));
  },
};
})();
